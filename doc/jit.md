在本项目中使用到了jit方法来加速模拟器的执行效率。

使用jit方法的前提是利用到了程序执行的局部性原理。因为程序执行过程的指令集合本身包含了很多相似的局部部分，或者说程序中存在大量的循环、跳转逻辑，使得具有这种时间、空间上的局部性。即当前执行的代码，可能会在不就得将来重复执行，当前访问的数据可能会在不久的将来重复访问。    
因此出于程序执行时间局部性的考虑，有理由相信机器可能在不久的将来会重复执行当前pc所指向的这段代码。所以在本项目中使用了哈希表的方式把需要模拟执行的`<pc, 在本地机器上执行的指令>`缓存起来。在模拟target架构的程序执行的时候，首先去检查缓存中是否有相同的pc地址，如果有的话，我们就可以直接执行host架构的相同逻辑的已经编译好的elf程序，不用再一条条的模拟取指、译码、执行这个循环了。从而达到提高性能的作用。  


首先分析一下jit执行的流程
```c
// 模拟器执行的函数
enum exit_reason_t machine_step(machine_t *m){
    while(true) {
        bool hot = true;

        // 根据当前机器的pc指针，在jit cache中检索，看看能不能找到相应的host的可执行代码片段
        u8 *code = cache_lookup(m->cache, m->state.pc);
        if (code == NULL) {
            // 找不到的话，更新这段代码的hot计数值，也就是对哈希表中pc这一项的hot值自增
            hot = cache_hot(m->cache, m->state.pc);
            if (hot) {
                // 如果pc这段代码是hot的，而且上面的cache_lookup中没找到相应的jit缓存
                // 那就说明pc这块代码还没编译生成host的代码片段，那就调用machine_genblock
                // 编译生成这段host代码片段。
                str_t source = machine_genblock(m);
                // source就是host的代码的字符串
                // 然后编译成一段代码code，而且把jit的代码已经加入到jit cache中
                // 在`machine_compile`函数中，使用了匿名管道的方式，把source编译生成的
                // elf链接文件拷贝到machine.cache中，并且在哈希表的pc项的offset记录
                // 这个elf文件在整个jit cache中的偏移

                // 最后的code就是jit cachezhong elf代码片段的内存地址
                code = machine_compile(m, source);
            }
        }

        // 如果不是hot，就还是按照取指、译码、执行这样一步一步来做
        if (!hot) {
            code = (u8 *)exec_block_interp;
        }
        while (true) {
            m->state.exit_reason = none;
            // 在这个地方，把code类型转换为函数指针，直接调用host缓存的函数
            // 不需要模拟执行了
            ((exec_block_func_t)code)(&m->state);
            assert(m->state.exit_reason != none);

            // 
            // 这儿有一个问题，给一个pc指针，如果要用jit的方式将host的指令编译的话，
            // 怎么选择jit的范围。最开始想到的是jit能转换的肯定是一段没有跳转的代码
            // 所以就是遇到跳转指令、ecall系统调用指令的时候停下来。
            // 从pc指针到停下来的范围内都是jit可以编译的一段代码。

            if (m->state.exit_reason == indirect_branch ||
                m->state.exit_reason == direct_branch ) {
                code = cache_lookup(m->cache, m->state.reenter_pc);
                if (code != NULL) continue;
            }

            if (m->state.exit_reason == interp) {
                m->state.pc = m->state.reenter_pc;
                code = (u8 *)exec_block_interp;
                continue;
            }

            break;
        }

        m->state.pc = m->state.reenter_pc;
        switch (m->state.exit_reason) {
        case direct_branch:
        case indirect_branch:
            // continue execution
            break;
        case ecall:
            return ecall;
        default:
            unreachable();
        }
    }
}
```

还有一些问题
- jit生成host的代码的时候，根据pc指针怎么确定要转换的代码范围的？是不是遇到跳转、ecall、还有读写csr等架构相关的寄存器的时候停止吗？这块代码对应`codegen.c/machine_genblock`函数。


- `machine_genblock`生成的jit热点代码缓存的范围是怎么确定的？

machine_genblock() 函数根据当前的pc指针，不断地取指、译码，然后模拟执行的过程，生成本地的C代码，以字符串的方式返回。
然后交给machine_compile()函数，把这段生成的本地C代码编译成对象文件(`.o`文件)，然后按照elf文件的封装格式解析这个jit cache对象文件，最终把这个二进制的代码才能够静态加载到内存中，变成动态的。  
这块二进制代码的位置就在cache_t中的jitcode的某个偏移处。然后再cache_t的哈希表table中新添加一项<pc, offset in `cache_t.jitcode`>。然后下次取指执行的时候就可以在cache中检查对比pc地址在不在哈希表的缓存中了。
```c
// 整个哈希表
typedef struct {
  u8 *jitcode;    // 用来缓存jit的cache
  u64 offset;     // 当前已经使用了的jitcode的偏移
  cache_item_t table[CACHE_ENTRY_SIZE]; // 这是一个哈希表,记录<pc, offset in jitcode>
  // 记录的是这个pc地址对应的jit代码在jitcode中的偏移量
} cache_t;


//使用哈希表额度方式存储pc地址和相应的host本地指令的对应关系
typedef struct {
  u64 pc;       // key
  u64 hot;      // hot计数器，记录pc指针指向的这段代码的hot程度
  u64 offset;   // value, indicate therr offset in jitcode cache
} cache_item_t;
```


#### `machine_compile`函数
这块的IO重定向有点绕

```c
u8 *machine_compile(machine_t *m, str_t source) {
    // 这儿的dup和下面的dup2是一对相互的操作，因为在这中间要重定向`STDOUT_FILENO`
    // 也就是说这个fd会被挪作他用，这相当于一个赋值恢复。
    int saved_stdout = dup(STDOUT_FILENO);
    int outp[2];

    // 此时文件描述符只有0、1、2被使用
    // 此时pipe创建管道，使用了3、4
    // pipe[0]是读数据的fd
    // pipe[1]是写数据的fd
    if (pipe(outp) != 0) fatal("cannot make a pipe");
    // dup2把管道的写端重定向到标准输出1
    dup2(outp[1], STDOUT_FILENO);
    // 然后把管道原来的写端关闭，此时outp[1]和标准输出是等价的
    // 因为只要写不关闭、读端口会一直阻塞
    close(outp[1]);

    FILE *f;
    // 创建一个管道，内部实现是fork一个子进程，执行shell命令 
    // `clang -O3 -c -xc -o /dev/stdout -`
    // 这个命令就是clang把源码编译成对象文件,写到标准输出
    f = popen("clang -O3 -c -xc -o /dev/stdout -", "w");
    if (f == NULL) fatal("cannot compile program");
    // 然后把codegen生成的host的c代码写到管道
    fwrite(source, 1, str_len(source), f);

    // 以shell命令的视角看的话，上面的fwrite就是从标准输入读入数据
    // 然后执行shell命令,再把命令执行结果输出到标准输出(`-o /dev/stdout`).
    // 但是此时标准输出已经被重定向到outp管道的输入一侧.
    // 接下来从管道的读端读数据就好的,也就是从outp[0]读数据

    // 这个pclose, 才相当于关闭了outp管道的写端
    pclose(f);
    fflush(stdout);

    read(outp[0], elfbuf, BINBUF_CAP);
    // 最后把重定向的标准输出恢复了
    dup2(saved_stdout, STDOUT_FILENO);

    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)elfbuf;

    // 然后根据elf文件的封装格式解析这个对象文件
    // ...
}
```

machine_compile()接下来的部分就是按照elf文件格式解析对象文件，然后把data段写入到jitcode中，并且设置jit的哈希表`<pc, offset>`。这块内容涉及到对象文件的elf格式，还不熟悉，可以看一下作者的另一个关于rv链接器的课程。


#### jit在host机器的C代码

在codegen中的`machine_genblock()`函数最后打印出source。然后让rvemu模拟器执行prime程序。可以看到在终端打印出的C代码类似于如下所示，可以看到每条rv64的指令被翻译成了一个C代码块。通过goto跳转。在codegen.c中的`DEFINE_TRACE_USAGE`就是记录了这条指令使用到了哪些寄存器并且把这个记录下来，在生成的C代码中体现出来就是对寄存器赋值恢复的这个过程。为什么要这么做，作者的解释是提高性能。可能是通过`state->`指针的方式访存开销比较大。
```c
#include <stdint.h>
#include <stdbool.h>
#define OFFSET 0x088800000000ULL               
#define TO_HOST(addr) (addr + OFFSET)          
enum exit_reason_t {                           
   none,                                       
   direct_branch,                              
   indirect_branch,                            
   interp,                                     
   ecall,                                      
};                                             
typedef union {                                
    uint64_t v;                                
    uint32_t w;                                
    double d;                                  
    float f;                                   
} fp_reg_t;                                    
typedef struct {                               
    enum exit_reason_t exit_reason;            
    uint64_t reenter_pc;                       
    uint64_t gp_regs[32];                      
    fp_reg_t fp_regs[32];                      
    uint64_t pc;                               
    uint32_t fcsr;                             
} state_t;                                     
void start(volatile state_t *restrict state) { 
    uint64_t x1 = state->gp_regs[1];
    uint64_t x2 = state->gp_regs[2];
    uint64_t x8 = state->gp_regs[8];
    uint64_t x10 = state->gp_regs[10];
    uint64_t x14 = state->gp_regs[14];
    uint64_t x15 = state->gp_regs[15];
    fp_reg_t f15 = state->fp_regs[15];
insn_101a2: {
    uint64_t rs1 = x2;
    x2 = rs1 + (int64_t)-48LL;
    goto insn_101a4;
}
insn_101a4: {
    uint64_t rs1 = x2;
    uint64_t rs2 = x1;
    *(uint64_t *)TO_HOST(rs1 + (int64_t)40LL) = (uint64_t)rs2;
    goto insn_101a6;
}
insn_101a6: {
    uint64_t rs1 = x2;
    uint64_t rs2 = x8;
    *(uint64_t *)TO_HOST(rs1 + (int64_t)32LL) = (uint64_t)rs2;
    goto insn_101a8;
}
insn_101a8: {
    uint64_t rs1 = x2;
    x8 = rs1 + (int64_t)48LL;
    goto insn_101aa;
}
insn_101aa: {
    uint64_t rs1 = x8;
    uint64_t rs2 = x10;
    *(uint64_t *)TO_HOST(rs1 + (int64_t)-40LL) = (uint64_t)rs2;
    goto insn_101ae;
}
insn_101ae: {
    uint64_t rs1 = x8;
    int64_t rd = *(int64_t *)TO_HOST(rs1 + (int64_t)-40LL);
    x14 = rd;
    goto insn_101b2;
}
insn_101b2: {
    uint64_t rs1 = 0;
    x15 = rs1 + (int64_t)1LL;
    goto insn_101b4;
}
insn_101b4: {
    uint64_t rs1 = x14;
    uint64_t rs2 = x15;
    if ((uint64_t)rs1 != (uint64_t)rs2) {
        goto insn_101bc;
    }
    goto insn_101b8;
}
insn_101b8: {
    uint64_t rs1 = 0;
    x15 = rs1 + (int64_t)0LL;
    goto insn_101ba;
}
insn_101ba: {
    goto insn_10218;
}
insn_10218: {
    uint64_t rs1 = 0;
    uint64_t rs2 = x15;
    x10 = rs1 + rs2;
    goto insn_1021a;
}
insn_1021a: {
    uint64_t rs1 = x2;
    int64_t rd = *(int64_t *)TO_HOST(rs1 + (int64_t)40LL);
    x1 = rd;
    goto insn_1021c;
}
insn_1021c: {
    uint64_t rs1 = x2;
    int64_t rd = *(int64_t *)TO_HOST(rs1 + (int64_t)32LL);
    x8 = rd;
    goto insn_1021e;
}
insn_1021e: {
    uint64_t rs1 = x2;
    x2 = rs1 + (int64_t)48LL;
    goto insn_10220;
}
insn_10220: {
    uint64_t rs1 = x1;
    state->exit_reason = indirect_branch;
    state->reenter_pc = (rs1 + (int64_t)0LL) & ~(uint64_t)1;
    goto end;
}
insn_101bc: {
    uint64_t rs1 = x8;
    int64_t rd = *(int64_t *)TO_HOST(rs1 + (int64_t)-40LL);
    x14 = rd;
    goto insn_101c0;
}
insn_101c0: {
    uint64_t rs1 = 0;
    x15 = rs1 + (int64_t)2LL;
    goto insn_101c2;
}
insn_101c2: {
    uint64_t rs1 = x14;
    uint64_t rs2 = x15;
    if ((uint64_t)rs1 != (uint64_t)rs2) {
        goto insn_101ca;
    }
    goto insn_101c6;
}
insn_101c6: {
    uint64_t rs1 = 0;
    x15 = rs1 + (int64_t)1LL;
    goto insn_101c8;
}
insn_101c8: {
    goto insn_10218;
}
insn_101ca: {
    uint64_t rs1 = 0;
    x15 = rs1 + (int64_t)2LL;
    goto insn_101cc;
}
insn_101cc: {
    uint64_t rs1 = x8;
    uint64_t rs2 = x15;
    *(uint32_t *)TO_HOST(rs1 + (int64_t)-20LL) = (uint32_t)rs2;
    goto insn_101d0;
}
insn_101d0: {
    goto insn_101ee;
}
insn_101ee: {
    uint64_t rs1 = x8;
    int64_t rd = *(int64_t *)TO_HOST(rs1 + (int64_t)-40LL);
    x10 = rd;
    goto insn_101f2;
}
insn_101f2: {
    x1 = 66038LL;
    goto insn_10294;
}
insn_10294: {
    uint64_t rs1 = x2;
    x2 = rs1 + (int64_t)-32LL;
    goto insn_10296;
}
insn_10296: {
    uint64_t rs1 = x2;
    uint64_t rs2 = x1;
    *(uint64_t *)TO_HOST(rs1 + (int64_t)24LL) = (uint64_t)rs2;
    goto insn_10298;
}
insn_10298: {
    uint64_t rs1 = x2;
    uint64_t rs2 = x8;
    *(uint64_t *)TO_HOST(rs1 + (int64_t)16LL) = (uint64_t)rs2;
    goto insn_1029a;
}
insn_1029a: {
    uint64_t rs1 = x2;
    x8 = rs1 + (int64_t)32LL;
    goto insn_1029c;
}
insn_1029c: {
    uint64_t rs1 = x8;
    uint64_t rs2 = x10;
    *(uint64_t *)TO_HOST(rs1 + (int64_t)-24LL) = (uint64_t)rs2;
    goto insn_102a0;
}
insn_102a0: {
    uint64_t rs1 = x8;
    int64_t rd = *(int64_t *)TO_HOST(rs1 + (int64_t)-24LL);
    x15 = rd;
    goto insn_102a4;
}
insn_102a4: {
    uint64_t rs1 = x15;
    f15.d = (double)(int64_t)rs1;
    goto insn_102a8;
}
insn_102a8: {
    state->exit_reason = interp;
    state->reenter_pc = 66216ULL;
    goto end;
}
end:;
    state->gp_regs[1] = x1;
    state->gp_regs[2] = x2;
    state->gp_regs[8] = x8;
    state->gp_regs[10] = x10;
    state->gp_regs[14] = x14;
    state->gp_regs[15] = x15;
    state->fp_regs[15] = f15;
}

```