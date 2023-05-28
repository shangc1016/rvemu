#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rvemu.h"
#include "types.h"

static void load_phdr(elf64_phdr_t *phdr, elf64_ehdr_t *ehdr, i64 i,
                      FILE *file) {
  // 先把文件之怎定位到相应ph位置
  if (fseek(file, ehdr->e_phoff + ehdr->e_phentsize * i, SEEK_SET) != 0) {
    fatal("load_phdr fail");
  }
  if (fread((void *)phdr, 1, ehdr->e_phentsize, file) != ehdr->e_phentsize) {
    fatal("load_phdr too small");
  }
}

// 从程序段的读写权限到mmap的读写执行权限的映射
static int flags_to_mmap_prot(u32 flags) {
  return (flags & PF_R ? PROT_READ : 0) | (flags & PF_W ? PROT_WRITE : 0) |
         (flags & PF_X ? PROT_EXEC : 0);
}

// static仅在本文件内使用
// 加载程序段
static void mmu_load_segment(mmu_t *mmu, elf64_phdr_t *phdr, int fd) {
  //
  int page_size = getpagesize();
  // 这个offset是这个程序段在elf文件中的偏移
  u64 offset = phdr->p_offset;
  // 这个vadrz 是这个程序段应该加载到的内存地址
  u64 vaddr = TO_HOST(phdr->p_vaddr);
  // 加载到的内存中的起始位置向下取整
  u64 aligned_vaddr = ROUNDDOWN(vaddr, page_size);
  // 这个filesz是这个程序段在elf文件中的大小
  u64 filesz = phdr->p_filesz + (vaddr - aligned_vaddr);
  // 这个memsz是这个程序段加载到内存中的真实占用大小
  u64 memsz = phdr->p_memsz + (vaddr - aligned_vaddr);

  // mmap: guest address sapce --mmap--> host address space 内存对齐的
  // 得到这个程序段的读写执行权限
  int prot = flags_to_mmap_prot(phdr->p_flags);
  // 使用mmap映射程序段
  // 映射的目的地址是aligned_vaddr，也就是返回值addr
  // 映射的长度是filesz，注意这个长度也是margin过的
  // offset就是程序段在elf文件中的偏移，注意这个offset也是margin过的
  u64 addr =
      (u64)mmap((void *)aligned_vaddr, filesz, prot, MAP_PRIVATE | MAP_FIXED,
                fd, ROUNDDOWN(offset, page_size));

  assert(addr == aligned_vaddr);

  // 有可能加上bss段的这块内存之后，roundup到了下一页
  // 那这种情况就需要把新的这个页面继续mmap到相应的地址
  u64 remaining_bss = ROUNDUP(memsz, page_size) - ROUNDUP(filesz, page_size);
  if (remaining_bss > 0) {
    u64 addr = (u64)mmap((void *)(aligned_vaddr + ROUNDUP(filesz, page_size)),
                         remaining_bss, prot, MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(addr == aligned_vaddr + ROUNDUP(filesz, page_size));
  }

  // 在host的mmap映射地址的最高处
  mmu->host_alloc =
      MAX(mmu->host_alloc, (aligned_vaddr + ROUNDUP(memsz, page_size)));
  // base指向的是代码段的结束，
  // 事实上，base就应该指向这个位置
  // alloc只是现在初始化也在这个位置而已
  // alloc是可移动的
  mmu->base = mmu->alloc = TO_GUEST(mmu->host_alloc);
}

// 根据elf文件，使用mmap把elf可执行文件的内容映射到内存地址
void mmu_load_elf(mmu_t *mmu, int fd) {
  
  u8 buf[sizeof(elf64_ehdr_t)];
  // 从文件中读到elf header
  FILE *file = fdopen(fd, "rb");
  if (fread(buf, 1, sizeof(elf64_ehdr_t), file) != sizeof(elf64_ehdr_t)) {
    // 如果读到的东西不等于elf头的大小
    fatal("file too small");
  }
  // step0.0: 把elf头读进来
  elf64_ehdr_t *ehdr = (elf64_ehdr_t *)buf;

  // step0.1: 检查elf文件头的魔数
  if (*(u32 *)ehdr != *(u32 *)ELFMAG) {
    fatal("bad elf file");
  }

  // step0.2: 检查elf文件的机器架构，machine架构，以及第四个字节(32位还是64位)
  if (ehdr->e_machine != EM_RISCV || ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
    fatal("only riscv64 elf file is supported");
  }

  // step1: 设置mmu的起始地址
  // 这个地址是个低位的地址
  mmu->entry = (u64)ehdr->e_entry;

  // step2: 读到每个program header
  elf64_phdr_t phdr;
  for (i64 i = 0; i < ehdr->e_phnum; i++) {
    // 把每个program header读入到phdr中，
    // 在elf header中保存了所有program header的起始偏移、以及program header的大小
    // 注意这个program header也属于元数据，所以只需要定义一个phdr，可以重复使用。
    load_phdr(&phdr, ehdr, i, file);

    // 如果这个program header的type是LOAD类型的话，就是需要加载到内存中的
    // 使用`riscv64-unknown-elf-readelf  -l <riscv64-program>`
    // 查看可以看到有三个ph，其中两个ph的类型是LOAD
    if (phdr.p_type == PT_LOAD) {
      mmu_load_segment(mmu, &phdr, fd);
    }
  }
  // 
  // 在上面把所有的程序段全部load之后，也是通过mmap的方式load之后，mmu中的host_alloc，alloc，base都指向了mmap的内存范围的最高地址
  // 保留host_alloc是为了后面分配内存的时候，作为mmap的起始地址
  // 
}

u64 mmu_alloc(mmu_t *mmu, i64 sz) {
  int page_size = getpagesize();
  u64 base = mmu->alloc;
  // 保证alloc 始终大于mmu->base
  assert(base >= mmu->base);

  mmu->alloc += sz;
  // 保证内存增加或者删除之后，肯定要比base起始地址大
  assert(mmu->alloc >= mmu->base);
  //  sz > 0，分配内存
  if(sz > 0 && mmu->alloc > TO_GUEST(mmu->host_alloc)) {
    // 如果是分配内存，用mmap继续映射到mmu->host_alloc的位置
    if(mmap((void *)mmu->host_alloc, ROUNDUP(sz, page_size), PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0) == MAP_FAILED){
        fatal(strerror(errno));
      }
    mmu->host_alloc += ROUNDUP(sz, page_size);
  } else if(sz < 0 && ROUNDUP(mmu->alloc, page_size) < TO_GUEST(mmu->host_alloc)) {
    // 如果释放了超过一页的内库，就用munmap释放掉一页的映射
    u64 len = TO_GUEST(mmu->host_alloc) - ROUNDUP(mmu->alloc, page_size);
    if(munmap((void*)ROUNDUP(mmu->alloc, page_size), len) == -1) {
      fatal(strerror(errno));
    }
    // munmap成功，在host上的mmap的最后地址缩减len
    mmu->host_alloc -= len;
  }
  return base;
}
