#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>

#include "rvemu.h"

// 所有的syscall
#define SYS_exit             93
#define SYS_exit_group       94
#define SYS_getpid          172
#define SYS_kill            129
#define SYS_tgkill          131
#define SYS_read             63
#define SYS_write            64
#define SYS_openat           56
#define SYS_close            57
#define SYS_lseek            62
#define SYS_brk             214
#define SYS_linkat           37
#define SYS_unlinkat         35
#define SYS_mkdirat          34
#define SYS_renameat         38
#define SYS_chdir            49
#define SYS_getcwd           17
#define SYS_fstat            80
#define SYS_fstatat          79
#define SYS_faccessat        48
#define SYS_pread            67
#define SYS_pwrite           68
#define SYS_uname           160
#define SYS_getuid          174
#define SYS_geteuid         175
#define SYS_getgid          176
#define SYS_getegid         177
#define SYS_gettid          178
#define SYS_sysinfo         179
#define SYS_mmap            222
#define SYS_munmap          215
#define SYS_mremap          216
#define SYS_mprotect        226
#define SYS_prlimit64       261
#define SYS_getmainvars    2011
#define SYS_rt_sigaction    134
#define SYS_writev           66
#define SYS_gettimeofday    169
#define SYS_times           153
#define SYS_fcntl            25
#define SYS_ftruncate        46
#define SYS_getdents         61
#define SYS_dup              23
#define SYS_dup3             24
#define SYS_readlinkat       78
#define SYS_rt_sigprocmask  135
#define SYS_ioctl            29
#define SYS_getrlimit       163
#define SYS_setrlimit       164
#define SYS_getrusage       165
#define SYS_clock_gettime   113
#define SYS_set_tid_address  96
#define SYS_set_robust_list  99
#define SYS_madvise         233
#define SYS_statx           291

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

// 80
static u64 sys_fstat(machine_t *m) {
    u64 fd = machine_get_gp_reg(m, a0);
    u64 addr = machine_get_gp_reg(m, a1);

    // 返回x86架构下的相同的syscall结果
    return fstat((int)fd, (struct stat *)TO_HOST(addr));
}

// 214: int brk(void *addr);
// brk系统调用把进程使用的内存地址的末尾设置为addr
static u64 sys_brk(machine_t *m) {
    // `int brk(void *addr)` 只有一个参数addr
    u64 addr = machine_get_gp_reg(m, a0);
    // 如果addr大于alloc，进行上取整，然后mmap，更新mmu.alloc
    if(addr == 0) return m->mmu.alloc;
    // 重新设定的mmu.alloc不能小于base，否则就是侵占了进程代码区域的内存了
    assert(addr > m->mmu.base);
    // 计算当前进程使用的内存地址大小和addr的差值
    i64 sz = (i64)addr - m->mmu.alloc;
    // 然后调用mmu_alloc，如果增加内存就继续在mmu.alloc后面mmap增加内存
    // 如果sz<0，就在把mmu.alloc-sz到mmu.alloc这段内存给munmap
    // 最后重新设置mmu.alloc
    mmu_alloc(&m->mmu, sz);
    return addr;
}

// 64: `ssize_t write(int fd, const void *buf, size_t count);`
static u64 sys_write(machine_t *m) {
    u64 fd = machine_get_gp_reg(m, a0);
    u64 buf = machine_get_gp_reg(m, a1);
    u64 count = machine_get_gp_reg(m, a2);
    // 调用host的syscall API
    return write(fd, (char *)TO_HOST(buf), (size_t)count);
}

// 57:
static u64 sys_close(machine_t *m) {
    u64 fd = machine_get_gp_reg(m, a0);
    // 调用host的close函数，如果要关闭0，1，2直接返回0，因为这三个fd是host使用的
    if (fd > 2) return close(fd);
    return 0;
}

// 93: 
static u64 sys_exit(machine_t *m) {
    u64 status = machine_get_gp_reg(m, a0);
    // 
    exit(status);
}

// 172
static u64 sys_getpid(machine_t *m) {
    // 
    return getpid();
}

// 129
static u64 sys_kill(machine_t *m) {
    u64 pid = machine_get_gp_reg(m, a0);
    u64 sig = machine_get_gp_reg(m, a1);
    // 
    return kill((pid_t)pid, (int)sig);
}

// 131: thread group kill
static u64 sys_tgkill(machine_t *m) {
    u64 tgid = machine_get_gp_reg(m, a0);
    u64 tid = machine_get_gp_reg(m, a1);
    u64 sig = machine_get_gp_reg(m, a2);
    return syscall(SYS_tgkill, tgid, tid, sig);
}

// 63
static u64 sys_read(machine_t *m) {
    // `ssize_t read(int fd, void *buf, size_t count)`
    u64 fd = machine_get_gp_reg(m, a0);
    u64 buf = machine_get_gp_reg(m, a1);
    u64 count = machine_get_gp_reg(m, a2);
    // 直接调用host的read这个syscall
    return read((int)fd, (void *)TO_HOST(buf), (size_t)count);
}


#define NEWLIB_O_RDONLY   0x0
#define NEWLIB_O_WRONLY   0x1
#define NEWLIB_O_RDWR     0x2
#define NEWLIB_O_APPEND   0x8
#define NEWLIB_O_CREAT  0x200
#define NEWLIB_O_TRUNC  0x400
#define NEWLIB_O_EXCL   0x800
#define REWRITE_FLAG(flag) if (flags & NEWLIB_##flag) hostflags |= flag;

static int convert_flags(int flags) {
    int hostflags = 0;
    REWRITE_FLAG(O_RDONLY);
    REWRITE_FLAG(O_WRONLY);
    REWRITE_FLAG(O_RDWR);
    REWRITE_FLAG(O_APPEND);
    REWRITE_FLAG(O_CREAT);
    REWRITE_FLAG(O_TRUNC);
    REWRITE_FLAG(O_EXCL); 
    return hostflags;
}

// 56:
static u64 sys_openat(machine_t *m) {
    u64 dirfd = machine_get_gp_reg(m, a0);
    u64 pathname = machine_get_gp_reg(m, a1);
    u64 flags = machine_get_gp_reg(m, a2);
    u64 mode = machine_get_gp_reg(m, a3);
    return openat((int)dirfd, (char *)TO_HOST(pathname), convert_flags(flags), (mode_t)mode);
}

// 62: 
static u64 sys_lseek(machine_t *m) {
    // `off_t lseek(int fd, off_t offset, int whence)`
    u64 fd = machine_get_gp_reg(m, a0);
    u64 offset = machine_get_gp_reg(m, a1);
    u64 whence = machine_get_gp_reg(m, a2);
    // 
    return lseek((int)fd, (__off_t)offset, (int)whence);
}

// 37
static u64 sys_linkat(machine_t *m) {
    // int linkat(int olddirfd, const char *oldpath,
    //            int newdirfd, const char *newpath, int flags);
    u64 fromfd = machine_get_gp_reg(m, a0);
    u64 from = machine_get_gp_reg(m, a1);
    u64 tofd = machine_get_gp_reg(m, a2);
    u64 to = machine_get_gp_reg(m, a3);
    u64 flags = machine_get_gp_reg(m, a4);
    // 
    return linkat(fromfd, (char *)TO_HOST(from), tofd, (char *)TO_HOST(to), flags);
}


// 35
static u64 sys_unlinkat(machine_t *m) {
    // int unlinkat(int dirfd, const char *pathname, int flags);
    u64 fd = machine_get_gp_reg(m, a0);
    u64 name = machine_get_gp_reg(m, a1);
    u64 flag = machine_get_gp_reg(m, a2);
    // 
    return unlinkat(fd, (char *)TO_HOST(name), flag);
}


// 169
static u64 sys_gettimeofday(machine_t *m) {
    // int gettimeofday(struct timeval *tv, struct timezone *tz);
    u64 tv_addr = machine_get_gp_reg(m, a0);
    u64 tz_addr = machine_get_gp_reg(m, a1);
    struct timeval * tv = (struct timeval*)TO_HOST(tv_addr);
    struct timezone *tz = NULL;
    if(tz_addr != 0) tz = (struct timezone *)TO_HOST(tz_addr);
    //
    return gettimeofday(tv, tz);
}

// 1024
static u64 sys_open(machine_t *m) {
    // int open(const char *pathname, int flags, mode_t mode);
    u64 pathname = machine_get_gp_reg(m, a0);
    u64 flags = machine_get_gp_reg(m, a1);
    u64 mode = machine_get_gp_reg(m, a2);
    // 
    return open((char *)TO_HOST(pathname), flags, (mode_t)mode);
}

// 因为syscall号码包括了old syscall，old syscall号码处于高位，为了节省sycall_table表
// 把syscall拆分成两个
// syscall table
static syscall_t syscall_table[] = {
    [SYS_exit           ] = sys_exit,
    [SYS_exit_group     ] = sys_exit,
    [SYS_getpid         ] = sys_getpid,
    [SYS_kill           ] = sys_kill,
    [SYS_tgkill         ] = sys_tgkill,
    [SYS_read           ] = sys_read,
    [SYS_write          ] = sys_write,
    [SYS_openat         ] = sys_openat,
    [SYS_close          ] = sys_close,
    [SYS_lseek          ] = sys_lseek,
    [SYS_brk            ] = sys_brk,
    [SYS_fstat          ] = sys_fstat,
    [SYS_linkat         ] = sys_linkat,
    [SYS_unlinkat       ] = sys_unlinkat,
    [SYS_gettimeofday   ] = sys_gettimeofday,
    [SYS_mkdirat        ] = sys_unimplemented,
    [SYS_renameat       ] = sys_unimplemented,
    [SYS_chdir          ] = sys_unimplemented,
    [SYS_getcwd         ] = sys_unimplemented,
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
    [SYS_open   - OLD_SYSCALL_THRESHOLD] = sys_open,
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