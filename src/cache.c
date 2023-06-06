#include "rvemu.h"

// 
// 这个文件主要是一个哈希表cache，记录<pc, code>这个jit code的缓存信息
// 这个demo的哈希表使用线性地址再探测的方法解决冲突
// 

#define sys_icache_invalidate(addr, size) \
    __builtin___clear_cache((char *)(addr), (char *)(addr) + (size));

static u64 hash(u64 pc) {
    // use hash(pc) as the key of jit cache.
    return pc % CACHE_ENTRY_SIZE;
}

cache_t *new_cache() {
    cache_t *cache = (cache_t *)calloc(1, sizeof(cache_t));
    // 使用mmap映射给cache->jitcode一大段内存，用来存放jit的code
    cache->jitcode = (u8 *)mmap(NULL, CACHE_SIZE, PROT_READ | PROT_WRITE |PROT_EXEC, 
                            MAP_ANONYMOUS | MAP_PRIVATE, -1 ,0);
    return cache;
}

#define MAX_SEARCH_COUNT 32
#define CACHE_HOT_COUNT 100000 // the threshold of whether hot
#define CACHE_IS_HOT (cache->table[index].hot >= CACHE_HOT_COUNT)

// 使用pc地址当做key检索jit的cache
u8 *cache_lookup(cache_t *cache, u64 pc) {
    assert(pc != 0);

    u64 index = hash(pc);
    while (cache->table[index].pc != 0) {
        if(cache->table[index].pc == pc) {
            // 如果是hot的话
            if (CACHE_IS_HOT) {
                // 返回cache->jitcode加上相应的pc地址对应的offset偏移
                return cache->jitcode + cache->table[index].offset;
            }
            break;
        }
        // 线性探测定址法
        index++;
        index = hash(index);
    }
    // 如果pc地址对应的这段代码没有在jitcache中缓存，或者不是hot的，那就直接返回NULL，表示没找到jit的代码
    return NULL;
}


// align `val` to n time's of `align`
static inline u64 align_to(u64 val, u64 align) {
    if(align == 0) return val;
    return (val + align - 1) & ~(align - 1);
}

// 添加一条<pc, offset>到jit cache
u8 *cache_add(cache_t *cache, u64 pc, u8 *code, size_t sz, u64 align) {
    cache->offset = align_to(cache->offset, align);
    // 确保在cache的jitcode中的offset位置写入sz长度的内容
    // CACHE_SIZE是在new_cache函数中alloc的jitcode的大小
    assert(cache->offset + sz <= CACHE_SIZE);

    u64 index = hash(pc);
    u64 search_count = 0;
    while(cache->table[index].pc != 0) {
        if(cache->table[index].pc == pc) {
            // 在cache中找到了相同的pc，说明在哈希表中已经缓存了pc的jit代码
            break;
        }
        // 线性再探测的哈希冲突解决方法
        index ++;
        index = hash(index);
        // 设置一个最大的线性探测次数阈值
        assert(++search_count <= MAX_SEARCH_COUNT);
    }

    // 此时的index索引就是code要放入的那个哈希的slot
    
    // 把pc对应的code拷贝到cache->jitcode的相应偏移量上
    memcpy(cache->jitcode + cache->offset, code, sz);
    // 更新这个hash slot的值
    cache->table[index].pc = pc;
    cache->table[index].offset = cache->offset;
    cache->offset += sz;
    // FIXME 这个宏是干啥的
    sys_icache_invalidate(cache->jitcode + cache->table[index].offset, sz);
    return cache->jitcode + cache->table[index].offset;
}

// 检查pc指针指向的这段jit cache是不是hot的；如果不是热点代码，会把哈希表中pc对应的这一项的hot值自增
bool cache_hot(cache_t *cache, u64 pc) {
    u64 index = hash(pc);
    u64 search_count = 0;

    while(cache->table[index].pc != 0) {
        if(cache->table[index].pc == pc) {
            // 先更新pc对应的hot计数
            cache->table[index].hot = MIN(++cache->table[index].hot, CACHE_HOT_COUNT);
            return CACHE_IS_HOT;
        }
        
        // 同样的线性地址再探测
        index++;
        index = hash(index);
        // assert限制线性地址再探测的次数
        assert(search_count <= MAX_SEARCH_COUNT);
    }
    // 如果在jit cache中没有找到pc这个key，那就把pc这条记录插入到jit cache中
    // 然后初始化它的hot数值
    cache->table[index].pc = pc;
    cache->table[index].hot = 1;
    return false;
}