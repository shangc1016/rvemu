#include <stdio.h>
#include "rvemu.h"


typedef void (func_t)(state_t *, insn_t *);

static func_t *funcs[] = {};

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
        // 这种情况指令需要跳转，先break出去
        if(insn.cont) break;
        // 如果指令不需要跳转，那就pc增加，继续执行
        // 此处检查这条指令是不是riscv 压缩指令，
        // 是的话指针后移2个字节16位，否则普通指令后移4个字节32位
        state->pc += insn.rvc ? 2 : 4;
    }
}
