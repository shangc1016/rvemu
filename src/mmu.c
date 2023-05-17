#include <stdio.h>

#include "rvemu.h"
#include "types.h"

void mmu_load_elf(mmu_t *mmu, int fd) {
  //
  u8 buf[sizeof(elf64_ehdr_t)];
  FILE *file = fdopen(fd, "rb");
  if (fread(buf, 1, sizeof(elf64_ehdr_t), file) != sizeof(elf64_ehdr_t)) {
    // 如果读到的东西不等于elf头的大小
    fatal("file too small");
  }
  // step0.0: 把elf头读进来
  elf64_ehdr_t *ehdr = (elf64_ehdr_t *)buf;

  // step0.1: 检查elf文件头
  if (*(u32 *)ehdr != *(u32 *)ELFMAG) {
    fatal("bad elf file");
  }

  // step0.2: 检查elf文件的机器架构，machine架构，以及第四个字节(32位还是64位)
  if (ehdr->e_machine != EM_RISCV || ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
    fatal("only riscv64 elf file is supported");
  }

  // step1: 设置mmu的起始地址
  mmu->entry = (u64)ehdr->e_entry;
}