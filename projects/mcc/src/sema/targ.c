#include <stdbool.h>
#include <string.h>
#include "util.h"
#include "mcc.h"

const struct target *targ;

static const struct target alltargs[] = {
	{
		.name = "x86_64-sysv",
		.typewchar = &typeint,
		.typevalist = &(struct type){
			.kind = TYPEARRAY,
			.align = 8, .size = 24,
			.base = &(struct type){
				.kind = TYPESTRUCT,
				.align = 8, .size = 24,
			},
		},
		.signedchar = 1,
	},
	{
		.name = "aarch64",
		.typevalist = &(struct type){
			.kind = TYPESTRUCT,
			.align = 8, .size = 32,
			.u.structunion.tag = "va_list",
		},
		.typewchar = &typeuint,
	},
	{
		.name = "riscv64",
		.typevalist = &(struct type){
			.kind = TYPEPOINTER, .prop = PROPSCALAR,
			.align = 8, .size = 8,
			.base = &typevoid,
		},
		.typewchar = &typeint,
	},
	{
		.name = "loongarch64",
		.typevalist = &(struct type){
			.kind = TYPEPOINTER, .prop = PROPSCALAR,
			.align = 8, .size = 8,
			.base = &typevoid,
		},
		.typewchar = &typeint,
	},
	{
		.name = "i386-sysv",
		.typevalist = &(struct type){
			.kind = TYPEPOINTER, .prop = PROPSCALAR,
			.align = 4, .size = 4,
			.base = &typevoid,
		},
		.typewchar = &typeint,
		.signedchar = 1,
	},
};

void
targinit(const char *name)
{
	size_t i;
	enum typequal qual;

	if (!name) {
		/* TODO: provide a way to set this default */
		targ = &alltargs[0];
	}
	for (i = 0; i < countof(alltargs) && !targ; ++i) {
		if (strcmp(alltargs[i].name, name) == 0)
			targ = &alltargs[i];
	}
	if (!targ)
		fatal("unknown target '%s'", name);
	typechar.u.arith.issigned = targ->signedchar;
	qual = QUALNONE;
	typeadjvalist = typeadjust(targ->typevalist, &qual);

	/* i386 is ILP32: int, long and pointers are all 4 bytes. The default
	 * `typelong`/`typeulong` (initialized in type.c) are 8 bytes for the
	 * LP64 targets (amd64/arm64/rv64). mkpointertype() reads typelong.size
	 * to size pointer types, so retuning long/ulong here propagates the
	 * correct width to every pointer built afterwards. long long stays 8. */
	if (strcmp(targ->name, "i386-sysv") == 0) {
		typelong.size = 4;
		typelong.align = 4;
		typelong.u.arith.width = 32;
		typeulong.size = 4;
		typeulong.align = 4;
		typeulong.u.arith.width = 32;
	}
}
