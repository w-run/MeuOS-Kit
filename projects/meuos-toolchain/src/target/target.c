/* target.c -- architecture target registry. */
#include "mt/target.h"
#include "mt/elf.h"

#include <string.h>

static const struct mt_target targets[] = {
	{"x86_64",      MT_EM_X86_64,     2, 1, 0,                64, 64},
	{"aarch64",     MT_EM_AARCH64,    2, 1, 0,                64, 64},
	{"riscv64",     MT_EM_RISCV,      2, 1, 0,                64, 64},
	{"loongarch64", MT_EM_LOONGARCH,  2, 1, 0x43,             64, 64},
	{"i386",        MT_EM_386,        1, 1, 0,                52, 40},
};

const struct mt_target mt_target_x86_64 = {
	"x86_64", MT_EM_X86_64, 2, 1, 0, 64, 64,
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
