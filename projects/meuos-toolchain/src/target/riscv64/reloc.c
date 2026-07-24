/* reloc.c — RISC-V relocation type definitions (psABI v1.0). */
#include <stdint.h>

#define R_RISCV_NONE      0
#define R_RISCV_32        1
#define R_RISCV_64        2
#define R_RISCV_RELATIVE  3
#define R_RISCV_COPY      4
#define R_RISCV_JUMP_SLOT 5
#define R_RISCV_TLS_DTPMOD32  6
#define R_RISCV_TLS_DTPMOD64  7
#define R_RISCV_TLS_DTPREL32  8
#define R_RISCV_TLS_DTPREL64  9
#define R_RISCV_TLS_TPREL32  10
#define R_RISCV_TLS_TPREL64  11
#define R_RISCV_BRANCH      16  /* B-type branch offset */
#define R_RISCV_JAL         17  /* J-type jal offset */
#define R_RISCV_CALL        18  /* auipc+jalr pair */
#define R_RISCV_CALL_PLT    19  /* auipc+jalr pair (PLT) */
#define R_RISCV_GOT_HI20    20  /* GOT entry auipc */
#define R_RISCV_TLS_GOT_HI20 21
#define R_RISCV_TLS_GD_HI20 22
#define R_RISCV_PCREL_HI20  23  /* auipc + %pcrel_hi */
#define R_RISCV_PCREL_LO12_I 24 /* load 12-bit I-type */
#define R_RISCV_PCREL_LO12_S 25 /* store 12-bit S-type */
#define R_RISCV_HI20         26  /* lui + %hi */
#define R_RISCV_LO12_I       27  /* load 12-bit I-type */
#define R_RISCV_LO12_S       28  /* store 12-bit S-type */
#define R_RISCV_ADD8         29
#define R_RISCV_ADD16        30
#define R_RISCV_ADD32        31
#define R_RISCV_ADD64        32
#define R_RISCV_SUB6         33
#define R_RISCV_SUB8         34
#define R_RISCV_SUB16        35
#define R_RISCV_SUB32        36
#define R_RISCV_SUB64        37
#define R_RISCV_TPREL_ADD    38
#define R_RISCV_TPREL_LO12_I 39
#define R_RISCV_TPREL_LO12_S 40
#define R_RISCV_TPREL_HI20   41
#define R_RISCV_TPREL_I      42  /* TP-relative 12-bit immediate */
#define R_RISCV_ALIGN        43
#define R_RISCV_RVC_BRANCH   44
#define R_RISCV_RVC_JAL      45
#define R_RISCV_CFLAGS       46
#define R_RISCV_ADD_32       47
#define R_RISCV_SUB_32       48
#define R_RISCV_SET6         49
#define R_RISCV_SET8         50
#define R_RISCV_SET16        51
#define R_RISCV_SET32        52
#define R_RISCV_32_PCREL     53
#define R_RISCV_BHJMP        54
#define R_RISCV_VENDOR_END   55

const char *
riscv64_reloc_name(unsigned type)
{
	switch (type) {
	case R_RISCV_NONE: return "R_RISCV_NONE";
	case R_RISCV_32: return "R_RISCV_32";
	case R_RISCV_64: return "R_RISCV_64";
	case R_RISCV_BRANCH: return "R_RISCV_BRANCH";
	case R_RISCV_JAL: return "R_RISCV_JAL";
	case R_RISCV_CALL: return "R_RISCV_CALL";
	case R_RISCV_CALL_PLT: return "R_RISCV_CALL_PLT";
	case R_RISCV_GOT_HI20: return "R_RISCV_GOT_HI20";
	case R_RISCV_PCREL_HI20: return "R_RISCV_PCREL_HI20";
	case R_RISCV_PCREL_LO12_I: return "R_RISCV_PCREL_LO12_I";
	case R_RISCV_PCREL_LO12_S: return "R_RISCV_PCREL_LO12_S";
	case R_RISCV_HI20: return "R_RISCV_HI20";
	case R_RISCV_LO12_I: return "R_RISCV_LO12_I";
	case R_RISCV_LO12_S: return "R_RISCV_LO12_S";
	case R_RISCV_TPREL_HI20: return "R_RISCV_TPREL_HI20";
	case R_RISCV_TPREL_LO12_I: return "R_RISCV_TPREL_LO12_I";
	case R_RISCV_TPREL_LO12_S: return "R_RISCV_TPREL_LO12_S";
	case R_RISCV_TPREL_ADD: return "R_RISCV_TPREL_ADD";
	case R_RISCV_ALIGN: return "R_RISCV_ALIGN";
	default: return "R_RISCV_??";
	}
}
