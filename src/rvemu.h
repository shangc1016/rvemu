#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "elfdef.h"
#include "types.h"
#include "reg.h"

// 宏，打印错误信息
#define fatalf(fmt, ...)                                           \
  (fprintf(stderr, "[fatal]: %s:%d " fmt "\n", __FILE__, __LINE__, \
           __VA_ARGS__),                                           \
   exit(1))
#define fatal(msg) fatalf("%s", msg)
#define unreachable() (fatal("unreachable"), __builtin_unreachable())

#define ROUNDDOWN(x, k) ((x) & -(k))
#define ROUNDUP(x, k) (((x) + (k)-1) & -(k))
#define MIN(x, y) ((y) > (x) ? (x) : (y))
#define MAX(x, y) ((y) < (x) ? (x) : (y))

#define GUEST_MEMORY_OFFSET 0x088800000000ULL

// riscv64 program -> local host
#define TO_HOST(addr) (addr + GUEST_MEMORY_OFFSET)
// local host -> riscv64
#define TO_GUEST(addr) (addr - GUEST_MEMORY_OFFSET)

// instruction
enum insn_type_t {
/* 0   */   insn_lb,
/* 1   */   insn_lh,
/* 2   */   insn_lw,
/* 3   */   insn_ld,
/* 4   */   insn_lbu,
/* 5   */   insn_lhu,
/* 6   */   insn_lwu,

/* 7   */   insn_fence,
/* 8   */   insn_fence_i,

/* 9   */   insn_addi,
/* 10  */   insn_slli,
/* 11  */   insn_slti,
/* 12  */   insn_sltiu,
/* 13  */   insn_xori,
/* 14  */   insn_srli,
/* 15  */   insn_srai,
/* 16  */   insn_ori,
/* 17  */   insn_andi,
/* 18  */   insn_auipc,
/* 19  */   insn_addiw,
/* 20  */   insn_slliw,
/* 21  */   insn_srliw,
/* 22  */   insn_sraiw,

/* 23  */   insn_sb,
/* 24  */   insn_sh,
/* 25  */   insn_sw,
/* 26  */   insn_sd,

/* 27  */   insn_add,
/* 28  */   insn_sll,
/* 29  */   insn_slt,
/* 30  */   insn_sltu,
/* 31  */   insn_xor,
/* 32  */   insn_srl,
/* 33  */   insn_or,
/* 34  */   insn_and,

/* 35  */   insn_mul,
/* 36  */   insn_mulh,
/* 37  */   insn_mulhsu,
/* 38  */   insn_mulhu,
/* 39  */   insn_div,
/* 40  */   insn_divu,
/* 41  */   insn_rem,
/* 42  */   insn_remu,

/* 43  */   insn_sub,
/* 44  */   insn_sra,
/* 45  */   insn_lui,

/* 46  */   insn_addw,
/* 47  */   insn_sllw,
/* 48  */   insn_srlw,
/* 49  */   insn_mulw,
/* 50  */   insn_divw,
/* 51  */   insn_divuw,
/* 52  */   insn_remw,
/* 53  */   insn_remuw,
/* 54  */   insn_subw,
/* 55  */   insn_sraw,

/* 56  */   insn_beq,
/* 57  */   insn_bne,
/* 58  */   insn_blt,
/* 59  */   insn_bge,
/* 60  */   insn_bltu,
/* 61  */   insn_bgeu,

/* 62  */   insn_jalr,
/* 63  */   insn_jal,
/* 64  */   insn_ecall,

/* 65  */   insn_csrrc,
/* 66  */   insn_csrrci,
/* 67  */   insn_csrrs,
/* 68  */   insn_csrrsi,
/* 69  */   insn_csrrw,
/* 70  */   insn_csrrwi,

/* 71  */   insn_flw,
/* 72  */   insn_fsw,

/* 73  */   insn_fmadd_s,
/* 74  */   insn_fmsub_s,
/* 75  */   insn_fnmsub_s,
/* 76  */   insn_fnmadd_s,
/* 77  */   insn_fadd_s,
/* 78  */   insn_fsub_s,
/* 79  */   insn_fmul_s,
/* 80  */   insn_fdiv_s,
/* 81  */   insn_fsqrt_s,

/* 82  */   insn_fsgnj_s,
/* 83  */   insn_fsgnjn_s,
/* 84  */   insn_fsgnjx_s,

/* 85  */   insn_fmin_s,
/* 86  */   insn_fmax_s,

/* 87  */   insn_fcvt_w_s,
/* 88  */   insn_fcvt_wu_s,
/* 89  */   insn_fmv_x_w,

/* 90  */   insn_feq_s,
/* 91  */   insn_flt_s,
/* 92  */   insn_fle_s,
/* 93  */   insn_fclass_s,
/* 94  */   insn_fcvt_s_w,
/* 95  */   insn_fcvt_s_wu,
/* 96  */   insn_fmv_w_x,
/* 97  */   insn_fcvt_l_s,
/* 98  */   insn_fcvt_lu_s,
/* 99  */   insn_fcvt_s_l,
/* 100 */   insn_fcvt_s_lu,
/* 101 */   insn_fld,
/* 102 */   insn_fsd,
/* 103 */   insn_fmadd_d,
/* 104 */   insn_fmsub_d,
/* 105 */   insn_fnmsub_d,
/* 106 */   insn_fnmadd_d,
/* 107 */   insn_fadd_d,
/* 108 */   insn_fsub_d,
/* 109 */   insn_fmul_d,
/* 110 */   insn_fdiv_d,
/* 111 */   insn_fsqrt_d,
/* 112 */   insn_fsgnj_d,
/* 113 */   insn_fsgnjn_d,
/* 114 */   insn_fsgnjx_d,
/* 115 */   insn_fmin_d,
/* 116 */   insn_fmax_d,
/* 117 */   insn_fcvt_s_d,
/* 118 */   insn_fcvt_d_s,
/* 119 */   insn_feq_d,
/* 120 */   insn_flt_d,
/* 121 */   insn_fle_d,
/* 122 */   insn_fclass_d,
/* 123 */   insn_fcvt_w_d,
/* 124 */   insn_fcvt_wu_d,
/* 125 */   insn_fcvt_d_w,
/* 126 */   insn_fcvt_d_wu,
/* 127 */   insn_fcvt_l_d,
/* 128 */   insn_fcvt_lu_d,
/* 129 */   insn_fmv_x_d,
/* 130 */   insn_fcvt_d_l,
/* 131 */   insn_fcvt_d_lu,
/* 132 */   insn_fmv_d_x,
/* 133 */   num_insns,

};

// decode.c
typedef struct {
  i8 rd;
  i8 rs1;
  i8 rs2;
  i8 rs3;
  i32 imm;
  i16 csr;                // 控制状态寄存器
  enum insn_type_t type;
  bool rvc;               // riscv compress riscsv压缩指令
  bool cont;              // 表示继续执行
} insn_t;

void insn_decode(insn_t *, u32);

// mmu.c
typedef struct {
  u64 entry;
  u64 host_alloc;
  u64 alloc;              // 指向的是进程动态分配的内存的一个地址
  u64 base;               // 指向的是ELF内容在内存中的占用
} mmu_t;

void mmu_load_elf(mmu_t *, int);
u64 mmu_alloc(mmu_t *, i64);

// 向内存中写数据
inline void mmu_write(u64 addr, u8 *data, size_t len) {
 memcpy((void*)TO_HOST(addr), (void*)data, len);
}

// state.c

// 译码执行的退出原因，可能是因为跳转指令而退出最里面的循环
// 也有可能是因为ecall系统调用跳转执行
enum exit_reason_t {
  none,
  direct_branch,          // 运行前知道的跳转
  indirect_branch,        // 运行时知道的跳转
  ecall,                  // syscall
};

// csr寄存器
enum csr_t {
  fflags = 0x001,
  frm    = 0x002,
  fcsr   = 0x003,
};

typedef struct {
  enum exit_reason_t exit_reason;
  u64 reenter_pc;
  u64 gp_regs[num_gp_regs];          // general propose
  fp_reg_t fp_regs[num_fp_regs];     // float register
  u64 pc;                            // pc pointer
} state_t;

// machine.c
typedef struct {
  state_t state;
  mmu_t mmu;
} machine_t;

enum exit_reason_t machine_step(machine_t *);
void machine_load_program(machine_t *, char *);
void machine_setup(machine_t *, int, char **);


// 下面这俩函数是为了在syscall之后，操控寄存器用的

// 获得寄存器的值
inline u64 machine_get_gp_reg(machine_t * machine, i32 reg) {
  assert(reg >= 0 && reg <= num_gp_regs);
  return machine->state.gp_regs[reg];
}

// 写入寄存器的值
inline void machine_set_gp_reg(machine_t * machine, i32 reg, u64 data) {
  assert(reg >= 0 && reg <= num_gp_regs);
  machine->state.gp_regs[reg] = data;
}

// interpret.c 
void exec_block_interp(state_t *);



// interpret_util.h
uint64_t mulhu(uint64_t a, uint64_t b);
int64_t mulh(int64_t a, int64_t b);
int64_t mulhsu(int64_t a, uint64_t b);
u32 fsgnj32(u32 a, u32 b, bool n, bool x);
u64 fsgnj64(u64 a, u64 b, bool n, bool x);
u16 f32_classify(f32 a);
u16 f64_classify(f64 a);