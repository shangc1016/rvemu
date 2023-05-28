#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "rvemu.h"


enum exit_reason_t machine_step(machine_t *m){
  while(true){
    m->state.exit_reason = none;
    // 指令翻译执行
    exec_block_interp(&m->state);


    assert(m->state.exit_reason != none);

    // 因为在指令执行中已经设置过了reenter_pc
    // 如果是跳转指令，将pc指针重设为reenter_pc，然后继续执行
    if(m->state.exit_reason == indirect_branch || 
      m->state.exit_reason == direct_branch){
        m->state.pc = m->state.reenter_pc;
        continue;
    }
    // 这儿break是因为遇到了syscall
    break;
  }

  m->state.pc = m->state.reenter_pc;
  // 先assert一下跳出原因就是syscall
  assert(m->state.exit_reason == ecall);
  // 这儿可以看到，如果跳出循环，就是遇到了syscall，
  // 需要在外层处理syscall，然后继续返回回来执行
  return ecall;
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

}