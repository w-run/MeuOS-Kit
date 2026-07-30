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
	{"arm",       MT_EM_ARM,        1, 1, 0x05000000,        52, 40, 0, arm_encode_insn},
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

/* Map a -march= value to an ISA feature bitmask for the given architecture.
 * Only x86_64 has ISA levels today; other architectures keep baseline. */
uint64_t
mt_target_features_for_march(const char *arch, const char *march)
{
	if (!arch || !march)
		return 0;

	if (strcmp(arch, "x86_64") != 0)
		return 0;

	/* "x86-64" → baseline (return 0) */
	if (strcmp(march, "x86-64") == 0)
		return 0;

	int level = -1;
	if (strncmp(march, "x86-64-v", 8) == 0 && march[8] >= '2' && march[8] <= '4'
	    && march[9] == '\0')
		level = march[8] - '0';
	else if (march[0] == 'v' && march[1] >= '2' && march[1] <= '4' && march[2] == '\0')
		level = march[1] - '0';

	if (level < 2 || level > 4)
		return 0;

	uint64_t f = MT_FEATURE_SSE | MT_FEATURE_SSE2 | MT_FEATURE_SSE3
	           | MT_FEATURE_SSSE3 | MT_FEATURE_SSE4_1 | MT_FEATURE_SSE4_2
	           | MT_FEATURE_POPCNT;
	if (level >= 3)
		f |= MT_FEATURE_AVX | MT_FEATURE_AVX2 | MT_FEATURE_BMI | MT_FEATURE_FMA;
	if (level >= 4)
		f |= MT_FEATURE_AVX512F;
	return f;
}

struct mt_target
mt_target_clone_with_features(const struct mt_target *base, uint64_t features)
{
	struct mt_target copy = *base;
	copy.features = features;
	return copy;
}

/* Single feature names, used to build ISA-gating diagnostics.  Order is
 * insignificant; we report the lowest missing bit. */
static const struct { uint64_t bit; const char *name; } feature_names[] = {
	{ MT_FEATURE_SSE,      "sse" },
	{ MT_FEATURE_SSE2,     "sse2" },
	{ MT_FEATURE_SSE3,     "sse3" },
	{ MT_FEATURE_SSSE3,    "ssse3" },
	{ MT_FEATURE_SSE4_1,   "sse4.1" },
	{ MT_FEATURE_SSE4_2,   "sse4.2" },
	{ MT_FEATURE_AVX,      "avx" },
	{ MT_FEATURE_AVX2,     "avx2" },
	{ MT_FEATURE_POPCNT,   "popcnt" },
	{ MT_FEATURE_BMI,      "bmi" },
	{ MT_FEATURE_FMA,      "fma" },
	{ MT_FEATURE_AVX512F,  "avx512f" },
	{ MT_FEATURE_FP16,     "fp16" },
	{ MT_FEATURE_SVE,      "sve" },
	{ MT_FEATURE_RV_F,     "rvf" },
	{ MT_FEATURE_RV_D,     "rvd" },
	{ MT_FEATURE_RV_C,     "rvc" },
	{ MT_FEATURE_RV_V,     "rvv" },
	{ MT_FEATURE_VFP,      "vfp" },
	{ MT_FEATURE_NEON,     "neon" },
	{ MT_FEATURE_THUMB,    "thumb" },
	{ MT_FEATURE_LSX,      "lsx" },
	{ MT_FEATURE_LASX,     "lasx" },
};

const char *
mt_feature_name_missing(uint64_t have, uint64_t required)
{
	uint64_t missing = required & ~have;
	size_t i;
	if (missing == 0)
		return NULL;
	for (i = 0; i < sizeof feature_names / sizeof feature_names[0]; ++i) {
		if (missing & feature_names[i].bit)
			return feature_names[i].name;
	}
	return "unknown";
}
