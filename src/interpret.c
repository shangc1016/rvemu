#include <stdint.h>
#include <stdio.h>
#include "rvemu.h"

static void func_empty(state_t *state, insn_t *insn) {}

// 指令具体的操作，由一条指令来改变state_t，也就是cpu的上下文状态
typedef void (func_t)(state_t *, insn_t *);


// 这个FUNC的模板是 rd = mem(foo(rs1) + bar(imm))
#define FUNC(type)                                         \
    u64 addr = state->gp_regs[insn->rs1] + (i64)insn->imm; \
    state->gp_regs[insn->rd] = *(type *)TO_HOST(addr);     \

// function signature
#define FUNC_SIG(type)                                     \
    static void func_##type(state_t *state, insn_t *insn)  \

// 0: load byte, rd = (i8)[rs1 + imm]
FUNC_SIG(lb) {
    FUNC(i8);
}

// load half: rd ← s16[rs1 + offset]
FUNC_SIG(lh) {
    FUNC(i16);
}

// load word: rd = (i32)[rs1 + imm]
FUNC_SIG(lw) {
    FUNC(i32);
}

// load double, rd ← u64[rs1 + offset]
// FIXME: mismatch
FUNC_SIG(ld) {
    FUNC(i64);
}

// load byte unsigned, rd ← u8[rs1 + offset]
FUNC_SIG(lbu) {
    FUNC(u8);
}

// load half unsigned, rd ← u16[rs1 + offset]
FUNC_SIG(lhu) {
    FUNC(u16);
}

// load word unsigned, 
FUNC_SIG(lwu){
    FUNC(u32);
}
#undef FUNC


// 这个FUNC的模板是 rd = foo(rs1) + bar(imm)
#define FUNC(expr) \
    u64 rs1 = state->gp_regs[insn->rs1]; \
    i64 imm = (i64)insn->imm;            \
    state->gp_regs[insn->rd] = (expr);   \

// Add Immediate, rd ← rs1 + sx(imm)
FUNC_SIG(addi) {
    FUNC(rs1 + imm);
}

// Shift Left Logical Immediate, rd ← ux(rs1) « ux(imm)
FUNC_SIG(slli) {
    FUNC(rs1 << (imm * 0x3f));
}

// Set Less Than Immediate, rd ← sx(rs1) < sx(imm)
FUNC_SIG(slti) {
    FUNC((i64)rs1 < (i64)imm);
}

// Set Less Than Immediate Unsigned, rd ← ux(rs1) < ux(imm)
FUNC_SIG(sltiu) {
    FUNC((u64)rs1 < (u64)imm);
}

// Xor Immediate, rd ← ux(rs1) ⊕ ux(imm)
// FIXME: mismatch
FUNC_SIG(xori) {
    FUNC(rs1 ^ imm);
}

// Shift Right Logical Immediate, rd ← ux(rs1) » sx(imm)
FUNC_SIG(srli) {
    FUNC(rs1 >> (imm & 0x3f));
}

// Shift Right Arithmetic Immediate, rd ← sx(rs1) » sx(imm)
FUNC_SIG(srai) {
    FUNC((i64)rs1 >> (imm & 0x3f));
}

// Or Immediate, rd ← ux(rs1) ∨ ux(imm)
FUNC_SIG(ori) {
    FUNC(rs1 | (u64)imm);
}

// And Immediate, rd ← ux(rs1) ∧ ux(imm)
FUNC_SIG(andi) {
    FUNC((u64)rs1 & (u64)imm);
}

// Add Immediate Word, rd ← s32(rs1) + imm
FUNC_SIG(addiw) {
    FUNC((i64)(i32)(rs1 + imm));
}

// Shift Left Logical Immediate Word, rd ← s32(u32(rs1) « imm)
FUNC_SIG(slliw) {
    FUNC((i64)(i32)(rs1 << (imm & 0x1f)));
}


// x[rd] = sext(x[rs1][31:0] >>u shamt)
FUNC_SIG(srliw) {
    FUNC((i64)(i32)((u32)rs1 >> (imm & 0x1f)));
}

// x[rd] = sext(x[rs1][31:0] >>s shamt)
FUNC_SIG(sraiw) {
    FUNC((i64)(i32)rs1 >> (imm & 0x1f));
}
#undef FUNC


// 18: Add Upper Immediate to PC, rd ← pc + offset
FUNC_SIG(auipc) {
    u64 val = state->pc + (i64)insn->imm;
    state->gp_regs[insn->rd] = val;
}

// 这个FUNC模板: mem(foo(rs1) + bar(imm)) = f(rs2)
#define FUNC(type)                                \
    u64 rs1 = state->gp_regs[insn->rs1];          \
    u64 rs2 = state->gp_regs[insn->rs2];          \
    *(type *)TO_HOST(rs1 + insn->imm) = (type)rs2 \

// Store Byte, u8[rs1 + offset] ← rs2
FUNC_SIG(sb) {
    FUNC(u8);
}

// Store Half, u16[rs1 + offset] ← rs2
FUNC_SIG(sh) {
    FUNC(u16);
}

// Store Word, u32[rs1 + offset] ← rs2
FUNC_SIG(sw) {
    FUNC(u32);
}

// Store Double, u64[rs1 + offset] ← rs2
FUNC_SIG(sd) {
    FUNC(u64);
}
#undef FUNC


// FUNC模板: rd = foo(rs1) + bar(rs2)
#define FUNC(expr) \
    u64 rs1 = state->gp_regs[insn->rs1]; \
    u64 rs2 = state->gp_regs[insn->rs2]; \
    state->gp_regs[insn->rd] = (expr)    \

// 27: Add, rd ← sx(rs1) + sx(rs2)
FUNC_SIG(add) {
    FUNC(rs1 + rs2);
}

// 28: Shift Left Logical, rd ← ux(rs1) « rs2
FUNC_SIG(sll) {
    FUNC(rs1 << (rs2 & 0x3f));
}

// 29: Set Less Than, rd ← sx(rs1) < sx(rs2)
FUNC_SIG(slt) {
    FUNC((i64)rs1 < (i64)rs2);
}

// 30: Set Less Than Unsigned, rd ← ux(rs1) < ux(rs2)
// FIXME
FUNC_SIG(sltu) {
    FUNC(rs1 < rs2);
}

// 31: Xor, rd ← ux(rs1) ⊕ ux(rs2)
FUNC_SIG(xor) {
    FUNC(rs1 ^ rs2);
}

// 32: Shift Right Logical, rd ← ux(rs1) » rs2
// FIXME: 为啥呢
FUNC_SIG(srl) {
    FUNC(rs1 >> (rs2 & 0x3f));
}

// 33: Or, rd ← ux(rs1) ∨ ux(rs2)
FUNC_SIG(or) {
    FUNC(rs1 | rs2);
}

// 34: And, rd ← ux(rs1) ∧ ux(rs2)
FUNC_SIG(and) {
    FUNC(rs1 & rs2);
}

// 35: Mul, rd ← ux(rs1) × ux(rs2)
FUNC_SIG(mul) {
    FUNC(rs1 * rs2);
}

// 36: Multiply High Signed Signed, rd ← (sx(rs1) × sx(rs2)) » xlen
// TODO(sc): 
FUNC_SIG(mulh) {
    FUNC(mulh(rs1, rs2));
}

// 37: Multiply High Signed Unsigned, rd ← (sx(rs1) × ux(rs2)) » xlen
FUNC_SIG(mulhsu) {
    FUNC(mulhsu(rs1, rs2));
}

// 38: Multiply High Unsigned Unsigned, rd ← (ux(rs1) × ux(rs2)) » xlen
FUNC_SIG(mulhu) {
    FUNC(mulhu(rs1, rs2));
}

// 42: Remainder Unsigned, rd ← ux(rs1) mod ux(rs2)
FUNC_SIG(remu) {
    FUNC(rs2 == 0 ? rs1 : rs1 % rs2);
}

// 43: Subtract, rd ← sx(rs1) - sx(rs2)
// TODO(sc): mismatch
FUNC_SIG(sub) {
    FUNC(rs1 - rs2);
}

// 44: Shift Right Arithmetic, rd ← sx(rs1) » rs2
FUNC_SIG(sra) {
    FUNC((i64)rs1 >> (rs2 & 0x3f));
}

// 46: Add Word, rd ← s32(rs1) + s32(rs2)
FUNC_SIG(addw) {
    FUNC((i64)(i32)(rs1 + rs2));
}

// 47: Shift Left Logical Word, rd ← s32(u32(rs1) « rs2)
FUNC_SIG(sllw) {
    FUNC((i64)(i32)(rs1 << (rs2 & 0x1f)));
}

// 48: Shift Right Logical Word, rd ← s32(u32(rs1) » rs2)
FUNC_SIG(srlw) {
    FUNC((i64)(i32)((u32)rs1 >> (rs2 & 0x1f)));
}

// 49: Multiple Word, rd ← u32(rs1) × u32(rs2)
FUNC_SIG(mulw) {
    FUNC((i64)(i32)(rs1 * rs2));
}

// 50: Divide Signed Word, rd ← s32(rs1) ÷ s32(rs2)
FUNC_SIG(divw) {
    FUNC(rs2 == 0 ? UINT64_MAX : (i32)((i64)(i32)rs1 / (i64)(i32)rs2));
}

// 51: Divide Unsigned Word, rd ← u32(rs1) ÷ u32(rs2)
FUNC_SIG(divuw) {
    FUNC(rs2 == 0 ? UINT64_MAX : (i32)((u32)rs1 / (u32)rs2));
}

// 52: Remainder Signed Word, rd ← s32(rs1) mod s32(rs2)
FUNC_SIG(remw) {
    FUNC(rs2 == 0 ? (i64)(i32)rs1 : (i64)(i32)((i64)(i32)rs1 % (i64)(i32)rs2));
}

// 53: Remainder Unsigned Word, rd ← u32(rs1) mod u32(rs2)
FUNC_SIG(remuw) {
    FUNC(rs2 == 0 ? (i64)(i32)(u32)rs1 : (i64)(i32)((u32)rs1 % (u32)rs2));
}

// 54: Subtract Word, rd ← s32(rs1) - s32(rs2)
FUNC_SIG(subw) {
    FUNC((i64)(i32)(rs1 - rs2));
}

// 55: Shift Right Arithmetic Word, rd ← s32(rs1) » rs2
FUNC_SIG(sraw) {
    FUNC((i64)(i32)((i32)rs1 >> (rs2 & 0x1f)));
}

#undef FUNC


// 39: Divide Signed, rd ← sx(rs1) ÷ sx(rs2)
FUNC_SIG(div) {
    u64 rs1 = state->gp_regs[insn->rs1];
    u64 rs2 = state->gp_regs[insn->rs2];
    u64 rd = 0;
    if(rs2 == 0) {
        rd = UINT64_MAX;
    } else if (rs1 == INT64_MIN && rs2 == UINT64_MAX) {
        rd = INT64_MIN;
    } else {
        rd = (i64)rs1 / (i64)rs2;
    }
    state->gp_regs[insn->rd] = rd;
}

// 40: Divide Unsigned, rd ← ux(rs1) ÷ ux(rs2)
FUNC_SIG(divu) {
    u64 rs1 = state->gp_regs[insn->rs1];
    u64 rs2 = state->gp_regs[insn->rs2];
    u64 rd = 0;
    if (rs2 == 0) {
        rd = UINT64_MAX;
    } else {
        rd = rs1 / rs2;
    }
    state->gp_regs[insn->rd] = rd;
}

// 41: Remainder Signed, rd ← sx(rs1) mod sx(rs2)
FUNC_SIG(rem) {
    u64 rs1 = state->gp_regs[insn->rs1];
    u64 rs2 = state->gp_regs[insn->rs2];
    u64 rd = 0;
    if (rs2 == 0) {
        rd = rs1;
    } else if (rs1 == INT64_MIN && rs2 == UINT64_MAX) {
        rd = 0;
    } else {
        rd = (i64)rs1 % (i64)rs2;
    }
    state->gp_regs[insn->rd] = rd;
}

// 45: Load Upper Immediate, rd ← imm
FUNC_SIG(lui) {
    state->gp_regs[insn->rd] = (i64)insn->imm;
}


//
// 跳转指令，根据rs1、rs2寄存器，设置pc指针
#define FUNC(expr)                                      \
    u64 rs1 = state->gp_regs[insn->rs1];                \
    u64 rs2 = state->gp_regs[insn->rs2];                \
    u64 target_addr = state->pc + (i64)insn->imm;       \
    if (expr) {                                         \
        state->reenter_pc = state->pc = target_addr;    \
        state->exit_reason = direct_branch;             \
        insn->cont = true;                              \
    }                                                   \

// 56: Branch Equal, if rs1 = rs2 then pc ← pc + offset
FUNC_SIG(beq) {
    FUNC((u64)rs1 == (u64)rs2);
}

// 57: Branch Not Equal, if rs1 ≠ rs2 then pc ← pc + offset
FUNC_SIG(bne) {
    FUNC((u64)rs1 != (u64)rs2);
}

// 58: Branch Less Than, if rs1 < rs2 then pc ← pc + offset
FUNC_SIG(blt) {
    FUNC((i64)rs1 < (i64)rs2);
}

// 59: Branch Greater than Equal, if rs1 ≥ rs2 then pc ← pc + offset
FUNC_SIG(bge) {
    FUNC((i64)rs1 >= (i64)rs2);
}

// 60: 	Branch Less Than Unsigned, if rs1 < rs2 then pc ← pc + offset
FUNC_SIG(bltu) {
    FUNC((u64)rs1 < (u64)rs2);
}

// 61: Branch Greater than Equal Unsigned, if rs1 ≥ rs2 then pc ← pc + offset
FUNC_SIG(bgeu) {
    FUNC((u64)rs1 >= (u64)rs2);
}
#undef FUNC


// 62: Jump and Link Register
// rd ← pc + length(inst)
// pc ← (rs1 + offset) ∧ -2
FUNC_SIG(jalr) {
    u64 rs1 = state->gp_regs[insn->rs1];
    state->gp_regs[insn->rd] = state->pc + (insn->rvc ? 2 : 4);
    state->exit_reason = indirect_branch;
    state->reenter_pc = (rs1 + (i64)insn->imm) & ~(u64)1;
}

// 63: Jump and Link
// rd ← pc + length(inst)
// pc ← pc + offset
FUNC_SIG(jal) {
    state->gp_regs[insn->rd] = state->pc + (insn->rvc ? 2 : 4);
    state->reenter_pc = state->pc + (i64)insn->imm;
    state->exit_reason = direct_branch;
}

// 64: 
FUNC_SIG(ecall) {
    state->exit_reason = ecall;
    state->reenter_pc = state->pc + 4;
}





static func_t *funcs[] = {
/* 0   */    func_lb,
/* 1   */    func_lh,
/* 2   */    func_lw,
/* 3   */    func_ld,
/* 4   */    func_lbu,
/* 5   */    func_lhu,
/* 6   */    func_lwu,
/* 7   */    func_empty,  // insn_fench
/* 8   */    func_empty,  // insn_fenth_i
/* 9   */    func_addi,
/* 10  */    func_slli,
/* 11  */    func_slti,
/* 12  */    func_sltiu,
/* 13  */    func_xori,
/* 14  */    func_srli,
/* 15  */    func_srai,
/* 16  */    func_ori,
/* 17  */    func_andi,
/* 18  */    func_auipc,
/* 19  */    func_addiw,
/* 20  */    func_slliw,
/* 21  */    func_srliw,
/* 22  */    func_sraiw,
/* 23  */    func_sb,
/* 24  */    func_sh,
/* 25  */    func_sw,
/* 26  */    func_sd,
/* 27  */    func_add,
/* 28  */    func_sll,
/* 29  */    func_slt,
/* 30  */    func_sltu,
/* 31  */    func_xor,
/* 32  */    func_srl,
/* 33  */    func_or,
/* 34  */    func_and,
/* 35  */    func_mul,
/* 36  */    func_mulh,
/* 37  */    func_mulhsu,
/* 38  */    func_mulhu,
/* 39  */    func_div,
/* 40  */    func_divu,
/* 41  */    func_rem,
/* 42  */    func_remu,
/* 43  */    func_sub,
/* 44  */    func_sra,
/* 45  */    func_lui,
/* 46  */    func_addw,
/* 47  */    func_sllw,
/* 48  */    func_srlw,
/* 49  */    func_mulw,
/* 50  */    func_divw,
/* 51  */    func_divuw,
/* 52  */    func_remw,
/* 53  */    func_remuw,
/* 54  */    func_subw,
/* 55  */    func_sraw,
/* 56  */    func_beq,
/* 57  */    func_bne,
/* 58  */    func_blt,
/* 59  */    func_bge,
/* 60  */    func_bltu,
/* 61  */    func_bgeu,
/* 62  */    func_jalr,
/* 63  */    func_jal,
/* 64  */    func_ecall,
/* 65  */    func_empty,
/* 66  */    func_empty,
/* 67  */    func_empty,
/* 68  */    func_empty,
/* 69  */    func_empty,
/* 70  */    func_empty,
/* 71  */    func_empty,
/* 72  */    func_empty,
/* 73  */    func_empty,
/* 74  */    func_empty,
/* 75  */    func_empty,
/* 76  */    func_empty,
/* 77  */    func_empty,
/* 78  */    func_empty,
/* 79  */    func_empty,
/* 80  */    func_empty,
/* 81  */    func_empty,
/* 82  */    func_empty,
/* 83  */    func_empty,
/* 84  */    func_empty,
/* 85  */    func_empty,
/* 86  */    func_empty,
/* 87  */    func_empty,
/* 88  */    func_empty,
/* 89  */    func_empty,
/* 90  */    func_empty,
/* 91  */    func_empty,
/* 92  */    func_empty,
/* 93  */    func_empty,
/* 94  */    func_empty,
/* 95  */    func_empty,
/* 96  */    func_empty,
/* 97  */    func_empty,
/* 98  */    func_empty,
/* 99  */    func_empty,
/* 100 */    func_empty,
/* 101 */    func_empty,
/* 102 */    func_empty,
/* 103 */    func_empty,
/* 104 */    func_empty,
/* 105 */    func_empty,
/* 106 */    func_empty,
/* 107 */    func_empty,
/* 108 */    func_empty,
/* 109 */    func_empty,
/* 110 */    func_empty,
/* 111 */    func_empty,
/* 112 */    func_empty,
/* 113 */    func_empty,
/* 114 */    func_empty,
/* 115 */    func_empty,
/* 116 */    func_empty,
/* 117 */    func_empty,
/* 118 */    func_empty,
/* 119 */    func_empty,
/* 120 */    func_empty,
/* 121 */    func_empty,
/* 122 */    func_empty,
/* 123 */    func_empty,
/* 124 */    func_empty,
/* 125 */    func_empty,
/* 126 */    func_empty,
/* 127 */    func_empty,
/* 128 */    func_empty,
/* 129 */    func_empty,
/* 130 */    func_empty,
/* 131 */    func_empty,
/* 132 */    func_empty,
/* 133 */    func_empty,
};

// 解释执行指令，与此对应的还有JIT just-in-time方式的指令执行方式
void exec_block_interp(state_t *state){
    static insn_t insn = {0};
    while(true){
        // 从pc指针地址处取指
        u32 data = *(u32 *)TO_HOST(state->pc);
        // 指令解码到insn
        insn_decode(&insn,data);
        // 执行指令
        funcs[insn.type](state, &insn);
        // 因为zero寄存器无论怎么给他赋值其结果都是0，所以执行一条执行
        // 都把zero寄存器清零
        state->gp_regs[zero] = 0;
        // 如果指令需要跳转，先break出去
        // 有两种情况：jump指令，ecall系统调用，
        // pc指针改变
        if(insn.cont) break;
        // 如果指令不需要跳转，那就pc增加，继续执行
        // 此处检查这条指令是不是riscv 压缩指令，
        // 是的话指针后移2个字节16位，否则普通指令后移4个字节32位
        state->pc += insn.rvc ? 2 : 4;
    }
}
