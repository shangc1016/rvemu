#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "rvemu.h"


enum exit_reason_t machine_step(machine_t *m){
  while(true){
    // 指令翻译执行
    exec_block_interp(&m->state);

    if(m->state.exit_reason == indirect_branch || 
      m->state.exit_reason == direct_branch){
        continue;
    }
    // 这儿break是因为遇到了syscall
    break;
  }
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