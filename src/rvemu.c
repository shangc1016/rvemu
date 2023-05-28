#include "rvemu.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  //
  // argv[1]是模拟器执行的可执行文件
  if (argc <= 1) {
    fatal("must give riscv exe file");
  }
  assert(argc > 1);

  machine_t machine = {0};
  
  // 加载elf可执行文件
  machine_load_program(&machine, argv[1]);
  // 初始化栈 32MB
  machine_setup(&machine, argc, argv);

  // 执行指令
  while(true){
    enum exit_reason_t exit_reason = machine_step(&machine);
    assert(exit_reason == ecall);
    // TODO: 在这儿处理syscall的逻辑
    printf("syscall\n");
  }

  return 0;
}