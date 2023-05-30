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
    // 
    // 发生syscall的时候，a7寄存器保存的就是syscall number
    // a0-a6保存的就是syscall的参数
    u64 syscall_num = machine_get_gp_reg(&machine, a7);
    
    // 用syscall_num查找syscall table，调用syscall的处理函数，最后得到返回值ret
    u64 ret = do_syscall(&machine, syscall_num);
    // 把syscall的返回值ret写入到a0寄存器，然后重新译码执行
    machine_set_gp_reg(&machine, a0, ret);

  }

  return 0;
}