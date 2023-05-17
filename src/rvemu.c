#include "rvemu.h"

#include <assert.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  //
  // argv[1]是模拟器执行的可执行文件
  if (argc <= 1) {
    fatal("must give riscv exe file");
  }
  assert(argc > 1);

  machine_t machine = {0};
  machine_load_program(&machine, argv[1]);

  printf("entry: 0x%lx\n", machine.mmu.entry);
  
  return 0;
}