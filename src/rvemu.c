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

  printf("entry: 0x%012lx\n", machine.mmu.entry);
  printf("TO_HOST(entry): 0x%012llx\n", TO_HOST(machine.mmu.entry));

  printf("host alloc: %lx\n", machine.mmu.host_alloc);
  
  return 0;
}