#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rvemu.h"
#include "types.h"

static void load_phdr(elf64_phdr_t *phdr, elf64_ehdr_t *ehdr, i64 i,
                      FILE *file) {
  // 先把文件之怎定位到相应ph位置
  if (fseek(file, ehdr->e_phoff + ehdr->e_phentsize * i, SEEK_SET) != 0) {
    fatal("load_phdr fail");
  }
  if (fread((void *)phdr, 1, ehdr->e_phentsize, file) != ehdr->e_phentsize) {
    fatal("load_phdr too small");
  }
}

static int flags_to_mmap_prot(u32 flags) {
  return (flags & PF_R ? PROT_READ : 0) | (flags & PF_W ? PROT_WRITE : 0) |
         (flags & PF_X ? PROT_EXEC : 0);
}

// static仅在本文件内使用
static void mmu_load_segment(mmu_t *mmu, elf64_phdr_t *phdr, int fd) {
  //
  int page_size = getpagesize();
  u64 offset = phdr->p_offset;
  u64 vaddr = TO_HOST(phdr->p_vaddr);
  u64 aligned_vaddr = ROUNDDOWN(vaddr, page_size);
  u64 filesz = phdr->p_filesz + (vaddr - aligned_vaddr);
  u64 memsz = phdr->p_memsz + (vaddr - aligned_vaddr);

  // mmap: guest address sapce --mmap--> host address space 内存对齐的

  int prot = flags_to_mmap_prot(phdr->p_flags);
  u64 addr =
      (u64)mmap((void *)aligned_vaddr, filesz, prot, MAP_PRIVATE | MAP_FIXED,
                fd, ROUNDDOWN(offset, page_size));

  assert(addr == aligned_vaddr);

  // 有可能加上bss段的这块内存之后，roundup到了下一页
  // 那这种情况就需要把新的这个页面继续mmap到相应的地址
  u64 remaining_bss = ROUNDUP(memsz, page_size) - ROUNDUP(filesz, page_size);
  if (remaining_bss > 0) {
    u64 addr = (u64)mmap((void *)(aligned_vaddr + ROUNDUP(filesz, page_size)),
                         remaining_bss, prot, MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(addr == aligned_vaddr + ROUNDUP(filesz, page_size));
  }

  mmu->host_alloc =
      MAX(mmu->host_alloc, (aligned_vaddr + ROUNDUP(memsz, page_size)));
  mmu->base = mmu->alloc = TO_GUEST(mmu->host_alloc);
}

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

  // step2: 读到每个program header
  elf64_phdr_t phdr;
  for (i64 i = 0; i < ehdr->e_phnum; i++) {
    load_phdr(&phdr, ehdr, i, file);

    // 如果这个program header的type是LOAD类型的话，就是需要加载到内存中的
    // 使用`riscv64-unknown-elf-readelf  -l playground/demo`
    // 查看可以看到有三个ph，其中两个ph的类型是LOAD
    if (phdr.p_type == PT_LOAD) {
      mmu_load_segment(mmu, &phdr, fd);
    }
  }
}