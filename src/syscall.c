#include <stdio.h>
#include "rvemu.h"

// 所有的syscall
#define SYS_exit              93
#define SYS_exit_group        94
#define SYS_getpid            172
#define SYS_kill              129
#define SYS_tgkill            131
#define SYS_read              63
#define SYS_write             64
#define SYS_openat            56
#define SYS_close             57
#define SYS_lseek             62
#define SYS_brk               214
#define SYS_linkat            37
#define SYS_unlinkat          35
#define SYS_mkdirat           34
#define SYS_renameat          38
#define SYS_chdir             49
#define SYS_getcwd            17
#define SYS_fstat             80
#define SYS_fstatat           79
#define SYS_faccessat         48
#define SYS_pread             67
#define SYS_pwrite            68
#define SYS_uname             160
#define SYS_getuid            174
#define SYS_geteuid           175
#define SYS_getgid            176
#define SYS_getegid           177
#define SYS_gettid            178
#define SYS_sysinfo           179
#define SYS_mmap              222
#define SYS_munmap            215
#define SYS_mremap            216
#define SYS_mprotect          226
#define SYS_prlimit64         261
#define SYS_getmainvars       2011
#define SYS_rt_sigaction      134
#define SYS_writev            66
#define SYS_gettimeofday      169
#define SYS_times             153
#define SYS_fcntl             25
#define SYS_ftruncate         46
#define SYS_getdents          61
#define SYS_dup               23
#define SYS_dup3              24
#define SYS_readlinkat        78
#define SYS_rt_sigprocmask    135
#define SYS_ioctl             29
#define SYS_getrlimit         163
#define SYS_setrlimit         164
#define SYS_getrusage         165
#define SYS_clock_gettime     113
#define SYS_set_tid_address   96
#define SYS_set_robust_list   99
#define SYS_madvise           233
#define SYS_statx             291

#define OLD_SYSCALL_THRESHOLD 1024
#define SYS_open              1024
#define SYS_link              1025
#define SYS_unlink            1026
#define SYS_mkdir             1030
#define SYS_access            1033
#define SYS_stat              1038
#define SYS_lstat             1039
#define SYS_time              1062


// 宏，得到某个寄存器的值
#define GET(name, reg) u64 name = machine_get_gp_reg(m, reg);

// syscall 处理函数，函数指针
typedef u64 (*syscall_t)(machine_t *);

// 未实现的syscall处理函数
static u64 sys_unimplemented(machine_t *m) {
    fatalf("unimplemented syscall, syscall_id = %ld", machine_get_gp_reg(m, a7));
    return 0; 
}

static u64 sys_fstat(machine_t *m) {
    // u64 fd = machine_get_gp_reg(m, a0);
    // u64 addr = machine_get_gp_reg(m, a1);
    // 这种写法不太习惯orz
    GET(fd, a0)
    GET(addr, a1)

    // 返回x86架构下的相同的syscall结果
    return fstat(fd, (struct stat*)TO_HOST(addr));
}

// int brk(void *addr);
// brk系统调用把进程使用的内存地址的末尾设置为addr
static u64 sys_brk(machine_t *m) {
    // `int brk(void *addr)` 只有一个参数addr
    u64 addr = machine_get_gp_reg(m, a0);
    // 首先判断addr是不是为0，如果是0，只需要返回当前进程的地址空间的大小
    // 计算addr和machine->mmu.alloc的大小关系、以及addr和machine->mmu.base的大小关系
    // 如果addr小于base, 进程的地址不应该设置的小于elf代码段，所以直接返回错误-1
    // 如果addr介于base和alloc之间，进行上取整，然后munmap，更新mmu.alloc
    // 如果addr大于alloc，进行上取整，然后mmap，更新mmu.alloc
    if(addr == 0) return m->mmu.alloc;
    
    u64 page_size = getpagesize();
    addr = ROUNDUP(addr, page_size);
    if (addr < m->mmu.base) {
        // brk缩小进程内存空间，而且小于elf内存映射部分的program段了
        return -1;
    } else if (m->mmu.base < addr && addr <= m->mmu.alloc) {
        // addr介于mmu.base和mmu.alloc之间，合法的缩减内存

    }
    return 0;
}


// 因为syscall号码包括了old syscall，old syscall号码处于高位，为了节省sycall_table表
// 把syscall拆分成两个    

// syscall table
static syscall_t syscall_table[] = {
    [SYS_exit           ] = sys_unimplemented,
    [SYS_exit_group     ] = sys_unimplemented,
    [SYS_getpid         ] = sys_unimplemented,
    [SYS_kill           ] = sys_unimplemented,
    [SYS_tgkill         ] = sys_unimplemented,
    [SYS_read           ] = sys_unimplemented,
    [SYS_write          ] = sys_unimplemented,
    [SYS_openat         ] = sys_unimplemented,
    [SYS_close          ] = sys_unimplemented,
    [SYS_lseek          ] = sys_unimplemented,
    [SYS_brk            ] = sys_brk,
    [SYS_linkat         ] = sys_unimplemented,
    [SYS_unlinkat       ] = sys_unimplemented,
    [SYS_mkdirat        ] = sys_unimplemented,
    [SYS_renameat       ] = sys_unimplemented,
    [SYS_chdir          ] = sys_unimplemented,
    [SYS_getcwd         ] = sys_unimplemented,
    [SYS_fstat          ] = sys_fstat,
    [SYS_fstatat        ] = sys_unimplemented,
    [SYS_faccessat      ] = sys_unimplemented,
    [SYS_pread          ] = sys_unimplemented,
    [SYS_pwrite         ] = sys_unimplemented,
    [SYS_uname          ] = sys_unimplemented,
    [SYS_getuid         ] = sys_unimplemented,
    [SYS_geteuid        ] = sys_unimplemented,
    [SYS_getgid         ] = sys_unimplemented,
    [SYS_getegid        ] = sys_unimplemented,
    [SYS_gettid         ] = sys_unimplemented,
    [SYS_sysinfo        ] = sys_unimplemented,
    [SYS_mmap           ] = sys_unimplemented,
    [SYS_munmap         ] = sys_unimplemented,
    [SYS_mremap         ] = sys_unimplemented,
    [SYS_mprotect       ] = sys_unimplemented,
    [SYS_prlimit64      ] = sys_unimplemented,
    [SYS_getmainvars    ] = sys_unimplemented,
    [SYS_rt_sigaction   ] = sys_unimplemented,
    [SYS_writev         ] = sys_unimplemented,
    [SYS_gettimeofday   ] = sys_unimplemented,
    [SYS_times          ] = sys_unimplemented,
    [SYS_fcntl          ] = sys_unimplemented,
    [SYS_ftruncate      ] = sys_unimplemented,
    [SYS_getdents       ] = sys_unimplemented,
    [SYS_dup            ] = sys_unimplemented,
    [SYS_dup3           ] = sys_unimplemented,
    [SYS_readlinkat     ] = sys_unimplemented,
    [SYS_rt_sigprocmask ] = sys_unimplemented,
    [SYS_ioctl          ] = sys_unimplemented,
    [SYS_getrlimit      ] = sys_unimplemented,
    [SYS_setrlimit      ] = sys_unimplemented,
    [SYS_getrusage      ] = sys_unimplemented,
    [SYS_clock_gettime  ] = sys_unimplemented,
    [SYS_set_tid_address] = sys_unimplemented,
    [SYS_set_robust_list] = sys_unimplemented,
    [SYS_madvise        ] = sys_unimplemented,
    [SYS_statx          ] = sys_unimplemented,
};


// old syscall table
static syscall_t old_syscall_table[] = {
    [SYS_open   - OLD_SYSCALL_THRESHOLD] = sys_unimplemented,
    [SYS_link   - OLD_SYSCALL_THRESHOLD] = sys_unimplemented,
    [SYS_unlink - OLD_SYSCALL_THRESHOLD] = sys_unimplemented,
    [SYS_mkdir  - OLD_SYSCALL_THRESHOLD] = sys_unimplemented,
    [SYS_access - OLD_SYSCALL_THRESHOLD] = sys_unimplemented,
    [SYS_stat   - OLD_SYSCALL_THRESHOLD] = sys_unimplemented,
    [SYS_lstat  - OLD_SYSCALL_THRESHOLD] = sys_unimplemented,
    [SYS_time   - OLD_SYSCALL_THRESHOLD] = sys_unimplemented,
};


#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

u64 do_syscall(machine_t *machine, u64 syscall_num) {
    syscall_t f = NULL;
    if(syscall_num < ARRAY_SIZE(syscall_table)) {
        // riscv syscall
        f = syscall_table[syscall_num];
    } else if (syscall_num - OLD_SYSCALL_THRESHOLD < ARRAY_SIZE(old_syscall_table)) {
        // old syscall
        f = old_syscall_table[syscall_num - OLD_SYSCALL_THRESHOLD];
    }
    if(f == NULL) {
        fatal("unknown syscall");
    }
    // 调用具体的syscall的处理函数
    return f(machine);
}