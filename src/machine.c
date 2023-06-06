#include "rvemu.h"


enum exit_reason_t machine_step(machine_t *m){
    while(true) {
        bool hot = true;

        // 根据当前机器的pc指针，在jit cache中检索，看看能不能找到相应的host的可执行代码片段
        u8 *code = cache_lookup(m->cache, m->state.pc);
        if (code == NULL) {
            // 找不到的话，更新这段代码的hot计数值
            hot = cache_hot(m->cache, m->state.pc);
            if (hot) {
                printf("pc = 0x%lx，生成jit代码缓存\n", m->state.pc);
                // 如果这段代码是hot的，而且在jit cache中没有缓存，那现在就编译成host的代码
                str_t source = machine_genblock(m);
                // source就是host的代码
                // 然后编译成一段代码code，而且把jit的代码已经加入到jit cache中
                code = machine_compile(m, source);
            }
        }

        // 如果不是hot，就还是按照取指、译码、执行这样一步一步来做
        if (!hot) {
            code = (u8 *)exec_block_interp;
        }
        // 
        while (true) {
            m->state.exit_reason = none;
            ((exec_block_func_t)code)(&m->state);
            assert(m->state.exit_reason != none);

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


void machine_load_program(machine_t *m, char *prog) {
  //
  // 从prog这个文件中读取可执行文件
  int fd = open(prog, O_RDONLY);
  if (fd == -1) {
    fatal(strerror(errno));
  }
  // 根据elf文件的格式解析mmu
  mmu_load_elf(&m->mmu, fd);
  close(fd);

  // 解析可执行文件elf之后，设置进程的pc指针
  m->state.pc = (u64)m->mmu.entry;
}

// 初始化栈
void machine_setup(machine_t *machine, int argc, char *argv[]) {
  // 栈空间的大小
  size_t stack_size = 32 * 1024 * 1024; // 32MB
  // 在elf文件的mmap地址之后，继mmap相应的内存空间作为栈空间
  u64 stack = mmu_alloc(&machine->mmu, stack_size);
  // 初始化栈顶指针sp到栈底位置
  machine->state.gp_regs[sp] = stack + stack_size;
  // 栈底保存着这几个变量auxv、envp、argv、argc
  // argv是个char*的数组，最后一项是0，留出来
  machine->state.gp_regs[sp] -= 8; // auxv
  machine->state.gp_regs[sp] -= 8; // envp
  machine->state.gp_regs[sp] -= 8; // argv end
  
  u64 args = argc - 1;
  // 对于每个argv[i]项，放入栈中
  for(int i = args; i > 0; i--){
    // 计算argv[i]的长度
    size_t len = strlen(argv[i]);
    // 继续调用mmu_alloc，调用mmap增加内存
    u64 addr = mmu_alloc(&machine->mmu, len + 1);
    // 把argv[i]写到分配出来的地址中
    mmu_write(addr, (u8 *)argv[i], len);
    // 栈指针sp后移
    machine->state.gp_regs[sp] -= 8;   // argv[i]
    // 把argv[i]的地址写到栈的sp位置
    mmu_write(machine->state.gp_regs[sp], (u8 *)&addr, sizeof(u64));
  }
  // 最后把argc写入栈中
  machine->state.gp_regs[sp] -= 8;
  mmu_write(machine->state.gp_regs[sp], (u8 *)&argc, sizeof(u64));

  // 最后的空间布局
  // 
  //  | <--代码段--> | <-------栈-------> <argc, argv[0] ... argv[argc - 1], envp, auxv>| 从发这个开始的地址都是mmap映射到的，封装成mmu_alloc函数
  //  ^mmap的起始地址 ^ mmu.base指向此处                                                  ^ mmu.alloc指向此处，随着mmu_alloc的调用不断后移，每次mmu_alloc都映射到mmu.alloc的位置
  //  

}