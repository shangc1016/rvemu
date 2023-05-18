#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "rvemu.h"
#include "types.h"

#define QUADRANT(data) (((data) >> 0) & 0x3)

// normal types
#define OPCODE(data) (((data) >>  2) & 0x1f)
#define RD(data)     (((data) >>  7) & 0x1f)
#define RS1(data)    (((data) >> 15) & 0x1f)
#define RS2(data)    (((data) >> 20) & 0x1f)
#define RS3(data)    (((data) >> 27) & 0x1f)
#define FUNCT2(data) (((data) >> 25) & 0x3 )
#define FUNCT3(data) (((data) >> 12) & 0x7 )
#define FUNCT7(data) (((data) >> 25) & 0x7f)
#define IMM116(data) (((data) >> 26) & 0x3f)

// 注意在解析指令的时候，先不要解析里面的functX，因为此时指令解析还没有完成
// 需要进一步的根据funct判断

// U-type
static inline insn_t insn_utype_read(u32 data){
  return (insn_t) {
    .imm = (i32)data & 0xfffff000,
    .rd = RD(data),
  };
}

// I-type
static inline insn_t insn_itype_read(u32 data){
  return (insn_t) {
    .imm = (i32)data >> 20,
    .rs1 = RS1(data),
    .rd = RD(data),
  };
}

// J-type
static inline insn_t insn_jtype_read(u32 data){
  u32 imm20    = (data >> 31) & 0x1;
  u32 imm10_1  = (data >> 21) & 0x3ff;
  u32 imm11    = (data >> 20) & 0x1;
  u32 imm19_12 = (data >> 12) & 0xff;

  i32 imm = (imm20 >> 20) | (imm19_12 >>12) | (imm11 << 11) | (imm10_1 << 1);
  imm = (imm << 11) >> 11;
  return (insn_t) {
    .imm = imm,
    .rd = RD(data),
  };
}

// B-type
static inline insn_t insn_btype_read(u32 data){
  u32 imm12  = (data >> 31) & 0x1;
  u32 imm105 = (data >> 25) & 0x3f;
  u32 imm41  = (data >>  8) & 0xf;
  u32 imm11  = (data >>  7) & 0x1;

  i32 imm = (imm12 << 12) | (imm11 << 11) | (imm105 << 5) | (imm41 << 1);
  imm = (imm << 9) >> 19;

  return (insn_t) {
    .imm = imm,
    .rs1 = RS1(data),
    .rs2 = RS2(data),
  };
}

// R-type
static inline insn_t insn_rtype_read(u32 data){
  return (insn_t) {
    .rs1 = RS1(data),
    .rs2 = RS2(data),
    .rd  = RD(data),
  };
}

// S-type
static inline insn_t insn_stype_read(u32 data){
  u32 imm115 = (data >> 25) & 0x7f;
  u32 imm40 = (data >>7) & 0x1f;

  i32 imm = (imm115 << 5) | imm40;
  imm = (imm << 20) >> 20;

  return (insn_t) {
    .imm = imm,
    .rs1 = RS1(data),
    .rs2 = RS2(data),
  };
}

// CSR-type
static inline insn_t insn_csrtype_read(u32 data){
  return (insn_t) {
    .csr = data >> 20,
    .rs1 = RS1(data),
    .rd = RD(data),
  };
}

// FPR-type
static inline insn_t insn_fprtype_read(u32 data){
  return (insn_t) {
    .rs1 = RS1(data),
    .rs2 = RS2(data),
    .rs3 = RS3(data),
    .rd = RD(data),
  };
}

// rvc compressed types
#define COPCODE(data)     (((data) >> 13) & 0x7 )
#define CFUNCT1(data)     (((data) >> 12) & 0x1 )
#define CFUNCT2LOW(data)  (((data) >>  5) & 0x3 )
#define CFUNCT2HIGH(data) (((data) >> 10) & 0x3 )
#define RP1(data)         (((data) >>  7) & 0x7 )
#define RP2(data)         (((data) >>  2) & 0x7 )
#define RC1(data)         (((data) >>  7) & 0x1f)
#define RC2(data)         (((data) >>  2) & 0x1f)

// 注意，压缩指令长度是16位的

// ca: 压缩的A类指令，Arithmetic
static inline insn_t insn_catype_read(u16 data){
  return (insn_t) {
    .rd = RP1(data) + 8,
    .rs2 = RP2(data) + 8,
    .rvc = true,          // 表示是压缩指令
  };
}

// cr: 压缩的R类型指令，Register
static inline insn_t insn_crtype_read(u16 data){
  return (insn_t) {
    .rs1 = RC1(data),
    .rs2 = RC2(data),
    .rvc = true,
  };
}

// ci: 压缩的I类型，Immediate
static inline insn_t insn_citype_read(u16 data){
  u32 imm40 = (data >>  2) & 0x1f;
  u32 imm5  = (data >> 12) & 0x1;
  i32 imm   = (imm5 <<  5) | imm40;
  imm = (imm << 26) >> 26;
  return (insn_t) {
    .imm = imm,
    .rd = RC1(data),
    .rvc = true,
  };
}

// ci2: 压缩的I类型，Immediate
static inline insn_t insn_citype_read2(u16 data){
  u32 imm86 = (data >>  2) & 0x7;
  u32 imm43 = (data >>  5) & 0x3;
  u32 imm5  = (data >> 12) & 0x1;

  i32 imm = (imm86 << 6) | (imm43 << 3) | (imm5 << 5);

  return (insn_t) {
    .imm = imm,
    .rd = RC1(data),
    .rvc = true,
  };
}


static inline insn_t insn_citype_read3(u16 data){
  u32 imm5  = (data >>  2) & 0x1;
  u32 imm87 = (data >>  3) & 0x3;
  u32 imm6  = (data >>  5) & 0x1;
  u32 imm4  = (data >>  6) & 0x1;
  u32 imm9  = (data >> 12) & 0x1;

  i32 imm = (imm5 << 5) | (imm87 << 7) | (imm6 << 6) | (imm4 << 4) | (imm9 << 9);

  imm = (imm << 22) >> 22;

  return (insn_t) {
    .imm = imm,
    .rd = RC1(data),
    .rvc = true,
  };
}

// 
static inline insn_t insn_citype_read4(u16 data){
  u32 imm5 = (data >> 12) & 0x1;
  u32 imm42 = (data >> 4) & 0x7;
  u32 imm76 = (data >> 2) & 0x3;

  i32 imm = (imm5 << 5) | (imm42 << 2) | (imm76 << 6);

  return (insn_t) {
    .imm = imm,
    .rd = RC1(data),
    .rvc = true,
  };
}

// 
static inline insn_t insn_citype_read5(u16 data) {
  u32 imm1612 = (data >> 2) & 0x1f;
  u32 imm17 = (data >> 12) & 0x1;

  i32 imm = (imm1612 << 12) | (imm17 << 17);
  imm = (imm << 14) >>14;

  return (insn_t) {
    .imm = imm,
    .rd = RC1(data),
    .rvc = true,
  };
}

// cb: 压缩的B类型，Branch
static inline insn_t insn_cbtype_read(u16 data) {
  u32 imm5 = (data >> 2) & 0x1;
  u32 imm21 = (data >> 3) & 0x3;
  u32 imm76 = (data >> 5) & 0x3;
  u32 imm43 = (data >> 10) & 0x3;
  u32 imm8 = (data >> 12) & 0x1;

  i32 imm = (imm8 << 8) | (imm76 << 6) | (imm5 << 5) | (imm43 << 3) || (imm21 << 1);
  imm = (imm << 23) >>23;

  return (insn_t) {
    .imm = imm,
    .rs1 = RP1(data) + 8,
    .rvc = true,
  };
}


static inline insn_t insn_cbtype_read2(u16 data) {
  u32 imm40 = (data >> 2) & 0x1f;
  u32 imm5 = (data >> 12) & 0x1;
  i32 imm = (imm5 << 5) | imm40;
  imm = (imm << 26) >> 26;

  return (insn_t) {
    .imm = imm,
    .rd = RP1(data) + 8,
    .rvc = true,
  };
}

// cs: 压缩的S类型，Store
static inline insn_t insn_cstype_read(u16 data) {
  u32 imm76 = (data >> 5) & 0x3;
  u32 imm53 = (data >> 10) & 0x7;

  i32 imm = ((imm76 << 6) | (imm53 << 3));

  return (insn_t) {
    .imm = imm,
    .rs1 = RP1(data) + 8,
    .rs2 = RP2(data) + 8,
    .rvc = true,
  };
}


static inline insn_t insn_cstype_read2(u16 data) {
  u32 imm6 = (data >> 5) & 0x1;
  u32 imm2 = (data >> 6) & 0x1;
  u32 imm53 = (data >> 10) & 0x7;

  i32 imm = ((imm6 << 6) | (imm2 << 2) | (imm53 << 3));
  return (insn_t) {
    .imm = imm,
    .rs1 = RP1(data) + 8,
    .rs2 = RP2(data) + 8,
    .rvc = true,
  };
}

// cj: 压缩的J类型，Jump
static inline insn_t insn_cjtype_read(u16 data) {
  u32 imm5  = (data >>  2) & 0x1;
  u32 imm31 = (data >>  3) & 0x7;
  u32 imm7  = (data >>  6) & 0x1;
  u32 imm6  = (data >>  7) & 0x1;
  u32 imm10 = (data >>  8) & 0x1;
  u32 imm98 = (data >>  9) & 0x3;
  u32 imm4  = (data >> 11) & 0x1;
  u32 imm11 = (data >> 12) & 0x1;

  i32 imm = ((imm5 << 5) | (imm31 << 1) | (imm7 << 7) | (imm6 << 6) | (imm10 << 10) | (imm98 << 8) | (imm4 << 4) | (imm11 << 11));

  imm = (imm << 20) >> 20;
  return (insn_t) {
    .imm = imm,
    .rvc = true,
  };
}

// 压缩的L类型，Load
static inline insn_t insn_cltype_read(u16 data) {
  u32 imm76 = (data >> 5) & 0x3;
  u32 imm53 = (data >> 10) & 0x7;

  i32 imm = ((imm76 << 6) | (imm53 << 3));

  return (insn_t) {
    .imm = imm,
    .rs1 = RP1(data) + 8,
    .rd = RP2(data) + 8,
    .rvc = true,
  };
}


static inline insn_t insn_cltype_read2(u16 data) {
  u32 imm76 = (data >> 5) & 0x3;
  u32 imm53 = (data >> 10) & 0x7;

  i32 imm = ((imm76 << 6) | (imm53 << 3));

  return  (insn_t) {
    .imm = imm,
    .rs1 = RP1(data) + 8,
    .rd = RP2(data) + 8,
    .rvc = true,
  };
}

// 压缩的SS类型，Stack-relative Store C.SDSP
static inline insn_t insn_csstype_read(u16 data) {
  u32 imm86 = (data >>  7) & 0x7;
  u32 imm53 = (data >> 10) & 0x7;

  i32 imm = ((imm86 << 6) | (imm53 <<3));
  return (insn_t) {
    .imm = imm,
     .rs2 = RC2(data),
     .rvc = true,
  };
}

// C.FSWSP
static inline insn_t insn_csstype_read2(u16 data) {
  u32 imm76 = (data >> 7) & 0x3;
  u32 imm52 = (data >> 9) & 0xf;

  i32 imm = ((imm76 << 6) | (imm52 << 2));

  return (insn_t) {
    .imm = imm,
    .rs2 = RC2(data),
    .rvc = true,
  };
}

// Immediate, 
static inline insn_t insn_ciwtype_read(u16 data) {
  u32 imm3  = (data >>  5) & 0x1;
  u32 imm2  = (data >>  6) & 0x1;
  u32 imm96 = (data >>  7) & 0xf;
  u32 imm54 = (data >> 11) & 0x3;

  i32 imm = ((imm3 << 3) | (imm2 << 2) | (imm96 << 6) | (imm54 << 4));
  return (insn_t) {
    .imm = imm,
    .rd = RP2(data) + 8,
    .rvc = true,
  };
}



// 指令解码
void insn_decode(insn_t *insn, u32 data) {
  u32 quadrant = QUADRANT(data);
  // 首先根据指令的象限分类switch，也就是根据指令的最后两位
  // 根据manual可知，前三个象限属于压缩指令，11属于普通指令
  switch (quadrant) {
    case 0x0: {
      // 压缩指令取出opcode
      u32 copcode = COPCODE(data);
      switch (copcode) {
      case 0x0:   // C.ADDI4SPN
        *insn = insn_ciwtype_read(data);
        insn->rs1 = sp;
        insn->type = insn_addi;
        assert(insn->imm != 0);
        return;
      case 0x1:   // C.FLD
        *insn = insn_cltype_read2(data);
        insn->type = insn_fld;
        return;
      case 0x2:   // C.LW
        *insn = insn_cltype_read(data);
        insn->type = insn_lw;
        return;
      case 0x3:   // C.LD
        *insn = insn_cltype_read2(data);
        insn->type = insn_ld;
        return;
      case 0x5:   // C.FSD
        *insn = insn_cstype_read(data);
        insn->type = insn_fsd;
        return;
      case 0x6:   // C.SW
        *insn = insn_cstype_read2(data);
        insn->type = insn_sw;
        return;
      case 0x7:   // C.SD
        *insn = insn_cstype_read(data);
        insn->type = insn_sd;
        return;
      default:
        printf("data: %x\n", data);
        fatal("unimplemented");
      }
    }
    unreachable();
    case 0x1:{
      // 第二象限的指令集
      u32 copcode = COPCODE(data);

      switch (copcode) {
        case 0x0:   // C.ADDI
          *insn = insn_citype_read(data);
          insn->type = insn->rd;
          insn->type = insn_addi;
          return;
        case 0x1:   // C.ADDIW
          *insn = insn_citype_read(data);
          assert(insn->rd != 0);
          insn->rs1 = insn->rd;
          insn->type = insn_addiw;
          return;
        case 0x2:   // C.LI
          *insn = insn_citype_read(data);
          insn->rs1 = zero;
          insn->type = insn_addi;
          return;
        case 0x3:{
          i32 rd = RC1(data);
          if(rd == 2) {   // C.ADDI16SP
            *insn = insn_citype_read3(data);
            assert(insn->imm != 0);
            insn->rs1 = insn->rd;
            insn->type = insn_addi;
            return;
          } else {        // C.LUI
            *insn = insn_citype_read5(data);
            assert(insn->imm != 0);
            insn->type = insn_lui;
            return;
          }
        }
        unreachable();
        case 0x4: {
          u32 cfunct2high = CFUNCT2HIGH(data);
          switch (cfunct2high) {
            case 0x0:   // C.SRLI64
            case 0x1:   // C.SRAI64
            case 0x2: { // C.ANDI
              *insn = insn_cbtype_read2(data);
              insn->rs1 = insn->rd;

              if(cfunct2high == 0x0) {
                insn->type = insn_srl;
              } else if(cfunct2high == 0x1) {
                insn->type = insn_srai;
              } else {
                insn->type = insn_andi;
              }
              return;
            }
            unreachable();
            case 0x3: {
              u32 cfunct1 = CFUNCT1(data);
              
              switch (cfunct1) {
                case 0x0: {
                  u32 cfunct2low = CFUNCT2LOW(data);

                  *insn = insn_catype_read(data);
                  insn->rs1 = insn->rd;
                  // 判断压缩指令的5,6位
                  switch (cfunct2low) {
                  case 0x0:     // C.SUB
                    insn->type = insn_sub;
                    break;
                  case 0x1:     // C.XOR
                    insn->type = insn_xor;
                    break;
                  case 0x2:     // C.OR
                    insn->type = insn_or;
                    break;
                  case 0x3:     // C.AND
                    insn->type = insn_and;
                    break;
                  default:
                    unreachable();
                  }
                  return;
                }
                unreachable();
                case 0x1: {
                  u32 cfunct2low = CFUNCT2LOW(data);

                  *insn = insn_catype_read(data);
                  insn->rs1 = insn->rd;

                  switch (cfunct2low) {
                    case 0x0:   // C.SUBW
                      insn->type = insn_subw;
                      break;
                    case 0x1:   // C.ADDW
                      insn->type = insn_addw;
                      break;
                    default:
                      unreachable();
                  }
                  return;
                }
                unreachable();
              }
              unreachable();

            }
          }
        }
        unreachable();
        case 0x5:
          *insn = insn_cjtype_read(data);
          insn->rd = zero;
          insn->type = insn_jal;
          insn->cont = true;
          return;
        case 0x6:
        case 0x7:
          *insn = insn_cbtype_read(data);
          insn->rs2 = zero;
          insn->type = copcode == 0x6 ? insn_beq : insn_bne;
          return;
        default:
          fatal("unrecognized copcode");
      }
    }
    case 0x2: {
      // 第三象限的指令集
      u32 copcode = COPCODE(data);
      switch (copcode) {
        case 0x0:     // C.SLLI
          *insn = insn_citype_read(data);
          insn->rs1 = insn->rd;
          insn->type = insn_slli;
          return;
        case 0x1:     // C.FLDSP
          *insn = insn_citype_read2(data);
          insn->rs1 = sp;
          insn->type = insn_fld;
          return;
        case 0x2:     // C.LWSP
          *insn = insn_citype_read4(data);
          assert(insn->rd != 0);
          insn->rs1 = sp;
          insn->type = insn_lw;
          return;
        case 0x3:     // C.LDSP
          *insn = insn_citype_read2(data);
          assert(insn->rd != 0);
          insn->rs1 = sp;
          insn->type = insn_ld;
          return;

        case 0x4: {
          u32 cfunct1 = CFUNCT1(data);
          switch(cfunct1) {
            case 0x0: {
              *insn = insn_crtype_read(data);
              if(insn->rs2 == 0){     // C.JR
                assert(insn->rs1 != 0);
                insn->rd = zero;
                insn->type = insn_jalr;
                insn->cont = true;
              } else {                // C.MV
                insn->rd = insn->rs1;
                insn->rs1 = zero;
                insn->type = insn_add;
              }
              return;
            }
            unreachable();
            case 0x1: {
              *insn = insn_crtype_read(data);
              if (insn->rs1 == 0 && insn->rs2 == 0) {   // C.ERBREAK
                fatal("unimplemented");
              } else if (insn->rs2 == 0) {              // C.JALR
                insn->rd = ra;
                insn->type = insn_jalr;
                insn->cont = true;
              } else {                                  // C.ADD
                insn->rd = insn->rs1;
                insn->type = insn_add;
              }
              return;
            }
            unreachable();
            default:
              unreachable();
          }
        }
        unreachable();
        case 0x5:       // C.FSDSP
          *insn = insn_csstype_read(data);
          insn->rs1 = sp;
          insn->type = insn_fsd;
          return;
        case 0x6:       // C.SWSP
          *insn = insn_csstype_read2(data);
          insn->rs1 = sp;
          insn->type = insn_sw;
          return;
        case 0x7:       // C.SDSP
          *insn = insn_csstype_read(data);
          insn->rs1 = sp;
          insn->type = insn_sd;
          return;
        default:
          fatal("unimplemented");
      }
    }
    unreachable();
    case 0x3:{
      // 第四象限的指令集，这个象限的指令集是不压缩的32位指令
      u32 opcode = OPCODE(data);
      switch (opcode) {
        case 0x0: {
          u32 funct3 = FUNCT3(data);

          *insn = insn_itype_read(data);
          switch (funct3) {
            case 0x0:       // LB
              insn->type = insn_lb;
              return;
            case 0x1:       // LH
              insn->type = insn_lh;
              return;
            case 0x2:       // LW
              insn->type = insn_lw;
              return;
            case 0x3:       // LD
              insn->type = insn_ld;
              return;
            case 0x4:       // LBU
              insn->type = insn_lbu;
              return;
            case 0x5:       // LHU
              insn->type = insn_lhu;
              return;
            case 0x6:       // LWU
              insn->type = insn_lwu;
              return;
            default:
              unreachable();
          }
        }
        unreachable();
        case 0x1: {
          u32 funct3 = FUNCT3(data);
          *insn = insn_itype_read(data);
          switch (funct3) {
            case 0x2:       // FLW
              insn->type = insn_flw;
              return;
            case 0x3:       // FLD
              insn->type = insn_fld;
              return;
            default:
              unreachable();
          }
        }
        unreachable();
        case 0x3: {
          u32 funct3 = FUNCT3(data);

          switch (funct3) {
            case 0x0: {     // FENCE
              insn_t _insn = {0};
              *insn = _insn;
              insn->type = insn_fence;
              return;
            }
            case 0x1: {     // FENCE.I
              insn_t _insn = {0};
              *insn = _insn;
              insn->type = insn_fence_i;
              return;
            }
            default:
              unreachable();
          }
        }
        unreachable();
        
        case 0x4: {
          u32 funct3 = FUNCT3(data);
          *insn = insn_itype_read(data);
          switch (funct3) {
            case 0x0:       // 
              insn->type = insn_addi;
              return;
            case 0x1: {
              u32 imm116 = IMM116(data);
              if (imm116 == 0) {  // SLLI
                insn->type = insn_slli;
              } else {
                unreachable();
              }
              return;
            }
            unreachable();
            case 0x2:       // SLTI
              insn->type = insn_slti;
              return;
            case 0x3:       // SLTIU
              insn->type = insn_sltiu;
              return;
            case 0x4:       // XORI
              insn->type = insn_xori;
              return;
            case 0x5: {
              u32 imm116 = IMM116(data);
              if (imm116 == 0x0) {          // SRLI
                insn->type = insn_srli;
              } else if (imm116 == 0x10) {  // SRAI
                insn->type = insn_srai;
              } else {
                unreachable();
              }
            }
            unreachable();
            case 0x6:     // ORI
              insn->type = insn_ori;
              return;
            case 0x7:     // ANDI
              insn->type = insn_andi;
              return;
            default:
              fatal("unimplemented funct3");
          }
        }
        unreachable();
        case 0x5:         // AUIPC
          *insn = insn_utype_read(data);
          insn->type = insn_auipc;
          return;
        case 0x6: {
          u32 funct3 = FUNCT3(data);
          u32 funct7 = FUNCT7(data);

          *insn = insn_itype_read(data);
          switch (funct3) {
            case 0x0:     // ADDIW
              insn->type = insn_addiw;
              return;
            case 0x1:     // SLLIW
              insn->type = insn_slliw;
              return;
            case 0x5: {
              switch (funct7) {
                case 0x0:  // SRLIW
                  insn->type = insn_srliw;
                  return;
                case 0x20:  // SRAIW
                  insn->type = insn_sraiw;
                  return;
                default:
                  unreachable();
              }
            }
            default:
              unreachable();
          }
        }
        unreachable();
        case 0x8: {
          u32 funct3 = FUNCT3(data);
          *insn = insn_stype_read(data);
          switch (funct3) {
            case 0x0:     // SB
              insn->type = insn_sb;
              return;
            case 0x1:     // SH
              insn->type = insn_sh;
              return;
            case 0x2:     // SW
              insn->type = insn_sw;
              return;
            case 0x3:     // SD
              insn->type = insn_sd;
              return;
            default:
              unreachable();
          }
        }
        unreachable();
        case 0x9: {
            u32 funct3 = FUNCT3(data);
            *insn = insn_stype_read(data);
            switch (funct3) {
              case 0x2:      // FSW
                insn->type = insn_fsw;
                return;
              case 0x3:     // FSD
                insn->type = insn_fsd;
                return;
              default:
                unreachable();
            }
        }
        unreachable();
        case 0xc: {
          *insn = insn_rtype_read(data);
          u32 funct3 = FUNCT3(data);
          u32 funct7 = FUNCT7(data);
          switch (funct7) {
            case 0x0: {
              switch (funct3) {
                case 0x0:
                  insn->type = insn_add;
                  return;
                case 0x1:
                  insn->type = insn_sll;
                  return;
                case 0x2:
                  insn->type = insn_slt;
                  return;
                case 0x3:
                  insn->type = insn_sltu;
                  return;
                case 0x4:
                  insn->type = insn_xor;
                  return;
                case 0x5:
                  insn->type = insn_srl;
                  return;
                case 0x6:
                  insn->type = insn_or;
                  return;
                case 0x7:
                  insn->type = insn_and;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x1: {
              switch (funct3) {
                case 0x0:
                  insn->type = insn_mul;
                  return;
                case 0x1:
                  insn->type = insn_mulh;
                  return;
                case 0x2:
                  insn->type = insn_mulhsu;
                  return;
                case 0x3:
                  insn->type = insn_mulhu;
                  return;
                case 0x4:
                  insn->type = insn_div;
                  return;
                case 0x5:
                  insn->type = insn_divu;
                  return;
                case 0x6:
                  insn->type = insn_rem;
                  return;
                case 0x7:
                  insn->type = insn_remu;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x20: {
              switch (funct3) {
                case 0x0:
                  insn->type = insn_sub;
                  return;
                case 0x5:
                  insn->type = insn_sra;
                  return;
                default:
                  unreachable();
              }
            }
            default:
              unreachable();
          }
        }
        case 0xd:
          *insn  =insn_utype_read(data);
          insn->type = insn_lui;
          return;
        case 0xe: {
          *insn = insn_rtype_read(data);

          u32 funct3 = FUNCT3(data);
          u32 funct7 = FUNCT7(data);

          switch (funct7) {
            case 0x0: {
              switch (funct3) {
                case 0x0:
                  insn->type = insn_addw;
                  return;
                case 0x1:
                  insn->type = insn_sllw;
                  return;
                case 0x5:
                  insn->type = insn_srlw;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x1: {
              switch (funct3) {
                case 0x0:
                  insn->type = insn_mulw;
                  return;
                case 0x4:
                  insn->type = insn_divw;
                  return;
                case 0x5:
                  insn->type = insn_divuw;
                  return;
                case 0x6:
                  insn->type = insn_remw;
                  return;
                case 0x7:
                  insn->type = insn_remuw;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x20: {
              switch (funct3) {
                case 0x0:
                  insn->type = insn_subw;
                  return;
                case 0x5:
                  insn->type = insn_sraw;
                  return;
                default:
                  unreachable();
              }
            }
            default:
              unreachable();
          }
        }
        unreachable();
        case 0x10: {
          u32 funct2 = FUNCT2(data);
          *insn = insn_fprtype_read(data);
          switch (funct2) {
            case 0x0:
              insn->type = insn_fmadd_s;
              return;
            case 0x1:
              insn->type = insn_fmadd_d;
              return;
            default:
              unreachable();
          }
        }
        unreachable();
        case 0x11: {
          u32 funct2 = FUNCT2(data);
          *insn = insn_fprtype_read(data);
          switch (funct2) {
            case 0x0:
              insn->type = insn_fmsub_s;
              return;
            case 0x1:
              insn->type = insn_fmsub_d;
              return;
            default:
              unreachable();
          }
        }
        unreachable();
        case 0x13: {
          u32 funct2 = FUNCT2(data);
          *insn = insn_fprtype_read(data);
          switch (funct2) {
            case 0x0:
              insn->type = insn_fnmadd_s;
              return;
            case 0x1:
              insn->type = insn_fnmadd_d;
              return;
            default:
              unreachable();
          }
        }

        unreachable();
        case 0x14: {
          u32 funct7 = FUNCT7(data);

          *insn = insn_rtype_read(data);
          switch (funct7) {
            case 0x0:
              insn->type = insn_fadd_s;
              return;
            case 0x1:
              insn->type = insn_fadd_d;
              return;
            case 0x4:
              insn->type = insn_fsub_s;
              return;
            case 0x5:
              insn->type = insn_fsub_d;
              return;
            case 0x8:
              insn->type = insn_fmul_s;
              return;
            case 0x9:
              insn->type = insn_fmul_d;
              return;
            case 0xc:
              insn->type = insn_fdiv_s;
              return;
            case 0xd:
              insn->type = insn_fdiv_d;
              return;
            case 0x10: {
              u32 funct3 = FUNCT3(data);
              switch (funct3) {
                case 0x0:
                  insn->type = insn_fsgnj_s;
                  return;
                case 0x1:
                  insn->type = insn_fsgnjn_s;
                  return;
                case 0x2:
                  insn->type = insn_fsgnjx_s;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x11: {
              u32 funct3 = FUNCT3(data);
              switch (funct3) {
                case 0x0:
                  insn->type = insn_fsgnj_d;
                  return;
                case 0x1:
                  insn->type = insn_fsgnjn_d;
                  return;
                case 0x2:
                  insn->type = insn_fsgnjx_d;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x14: {
              u32 funct3 = FUNCT3(data);
              switch (funct3) {
                case 0x0:
                  insn->type = insn_fmin_s;
                  return;
                case 0x1:
                  insn->type = insn_fmax_s;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x15: {
              u32 funct3 = FUNCT3(data);
              switch (funct3) {
                case 0x0:
                  insn->type = insn_fmin_d;
                  return;
                case 0x1:
                  insn->type = insn_fmax_d;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x20:
              assert(RS2(data) == 1);
              insn->type = insn_fcvt_s_d;
              return;
            case 0x21:
              assert(RS2(data) == 0);
              insn->type = insn_fcvt_d_s;
              return;
            case 0x2d:
              assert(insn->rs2 == 0);
              insn->type = insn_fsqrt_d;
              return;
            case 0x50: {
              u32 funct3 = FUNCT3(data);

              switch (funct3) {
                case 0x0:
                  insn->type = insn_fle_s;
                  return;
                case 0x1:
                  insn->type = insn_flt_s;
                  return;
                case 0x2:
                  insn->type = insn_feq_s;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x51: {
              u32 funct3 = FUNCT3(data);

              switch (funct3) {
                case 0x0:
                  insn->type = insn_fle_d;
                  return;
                case 0x1:
                  insn->type = insn_flt_d;
                  return;
                case 0x2:
                  insn->type = insn_feq_d;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x60: {
              u32 rs2 = RS2(data);
              switch (rs2) {
                case 0x0:
                  insn->type = insn_fcvt_w_s;
                  return;
                case 0x1:
                  insn->type = insn_fcvt_wu_s;
                  return;
                case 0x2:
                  insn->type = insn_fcvt_l_s;
                  return;
                case 0x3:
                  insn->type = insn_fcvt_lu_s;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x61: {
              u32 rs2 = RS2(data);

              switch (rs2) {
                case 0x0:
                  insn->type = insn_fcvt_w_d;
                  return;
                case 0x1:
                  insn->type = insn_fcvt_wu_d;
                  return;
                case 0x2:
                  insn->type = insn_fcvt_l_d;
                  return;
                case 0x3:
                  insn->type = insn_fcvt_lu_d;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x68: {
              u32 rs2 = RS2(data);
              switch (rs2) {
                case 0x0:
                  insn->type = insn_fcvt_s_w;
                  return;
                case 0x1:
                  insn->type = insn_fcvt_s_wu;
                  return;
                case 0x2:
                  insn->type = insn_fcvt_s_l;
                  return;
                case 0x3:
                  insn->type = insn_fcvt_s_lu;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x69: {
              u32 rs2 = RS2(data);
              switch (rs2) {
                case 0x0:
                  insn->type = insn_fcvt_d_w;
                  return;
                case 0x1:
                  insn->type = insn_fcvt_d_wu;
                  return;
                case 0x2:
                  insn->type = insn_fcvt_d_l;
                  return;
                case 0x3:
                  insn->type = insn_fcvt_d_lu;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x70: {
              assert(RS2(data) == 0);
              u32 funct3 = FUNCT3(data);
              switch (funct3) {
                case 0x0:
                  insn->type = insn_fmv_x_w;
                  return;
                case 0x1:
                  insn->type = insn_fclass_s;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x71: {
              assert(RS2(data) == 0);
              u32 funct3 = FUNCT3(data);
              switch (funct3) {
                case 0x0:
                  insn->type = insn_fmv_x_d;
                  return;
                case 0x1:
                  insn->type = insn_fclass_d;
                  return;
                default:
                  unreachable();
              }
            }
            unreachable();
            case 0x78:
              assert(RS2(data) == 0 &&FUNCT3(data) == 0);
              insn->type = insn_fmv_w_x;
              return;
            case 0x79:
              assert(RS2(data) == 0 &&FUNCT3(data) == 0);
              insn->type = insn_fmv_d_x;
              return;
            default:
              unreachable();
          }
        }
        unreachable();
        case 0x18: {
          *insn = insn_btype_read(data);
          u32 funct3 = FUNCT3(data);
          switch (funct3) {
            case 0x0:
              insn->type = insn_beq;
              return;
            case 0x1:
              insn->type = insn_bne;
              return;
            case 0x4:
              insn->type = insn_blt;
              return;
            case 0x5:
              insn->type = insn_bge;
              return;
            case 0x6:
              insn->type = insn_bltu;
              return;
            case 0x7:
              insn->type = insn_bgeu;
              return;
            default:
              unreachable();
          }
        }
        unreachable();
        case 0x19:
          *insn = insn_itype_read(data);
          insn->type = insn_jalr;
          insn->cont = true;
          return;
        case 0x1b:
          *insn = insn_jtype_read(data);
          insn->type = insn_jal;
          insn->cont = true;
          return;
        case 0x1c: {
          if(data == 0x73) {
            insn->type = insn_ecall;
            insn->cont = true;
            return;
          }
          u32 funct3 = FUNCT3(data);
          *insn = insn_csrtype_read(data);
          switch (funct3) {
            case 0x1:
              insn->type = insn_csrrw;
              return;
            case 0x2:
              insn->type = insn_csrrs;
              return;
            case 0x3:
              insn->type = insn_csrrc;
              return;
            case 0x5:
              insn->type = insn_csrrwi;
              return;
            case 0x6:
              insn->type = insn_csrrsi;
              return;
            case 0x7:
              insn->type = insn_csrrci;
              return;
            default:
              unreachable();
          }
        }
        default:
          unreachable();
      }
    }
    default:
      unreachable();
  }
}