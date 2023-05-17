#include <fcntl.h>
#include <string.h>

#include "rvemu.h"

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
}