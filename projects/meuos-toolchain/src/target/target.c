/* target.c -- architecture target registry. */
#include "mt/target.h"
#include "mt/elf.h"

#include <string.h>

/* Forward declarations for target-specific instruction encoders. */
extern int x86_64_encode_insn(const struct mt_target *target,
                              const char *mnemonic, const char *operands,
                              struct mt_insn *out);
extern int i386_encode_insn(const struct mt_target *target,
                            const char *mnemonic, const char *operands,
                            struct mt_insn *out);
extern int aarch64_encode_insn(const struct mt_target *target,
                               const char *mnemonic, const char *operands,
                               struct mt_insn *out);
extern int riscv64_encode_insn(const struct mt_target *target,
                               const char *mnemonic, const char *operands,
                               struct mt_insn *out);
extern int la64_encode_insn(const struct mt_target *target,
                            const char *mnemonic, const char *operands,
                            struct mt_insn *out);
extern int arm_encode_insn(const struct mt_target *target,
                             const char *mnemonic, const char *operands,
                             struct mt_insn *out);

static const struct mt_target targets[] = {
	{"x86_64",      MT_EM_X86_64,     2, 1, 0,                64, 64, 0, x86_64_encode_insn},
	{"aarch64",     MT_EM_AARCH64,    2, 1, 0,                64, 64, 0, aarch64_encode_insn},
	{"riscv64",     MT_EM_RISCV,      2, 1, 0,                64, 64, 0, riscv64_encode_insn},
	{"loongarch64", MT_EM_LOONGARCH,  2, 1, 0,                 64, 64, 0, la64_encode_insn},
	{"arm",       MT_EM_ARM,        1, 1, 0x04000000,        52, 40, 0, arm_encode_insn},
	{"i386",        MT_EM_386,        1, 1, 0,                52, 40, 0, i386_encode_insn},
};

const struct mt_target mt_target_x86_64 = {
	"x86_64", MT_EM_X86_64, 2, 1, 0, 64, 64, MT_FEATURE_SSE2, x86_64_encode_insn,
};

const struct mt_target *
mt_target_lookup(const char *name)
{
	size_t i;
	for (i = 0; i < sizeof targets / sizeof targets[0]; ++i) {
		if (strcmp(targets[i].name, name) == 0)
			return &targets[i];
	}
	return NULL;
}
