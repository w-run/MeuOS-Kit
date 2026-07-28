/* cpu_detect.c — Host CPU feature detection via CPUID and /proc/cpuinfo.
 *
 * Provides detect_cpu_features() which returns a bitmask of MT_FEATURE_*
 * flags describing the host CPU's ISA capabilities.  Used by -march=native
 * to enable instruction-set extensions available on the build machine. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mt/target.h"

/* x86_64 CPUID wrapper (GCC/Clang inline assembly). */
static void
x86_cpuid(uint32_t leaf, uint32_t subleaf,
          uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ volatile(
		"cpuid"
		: "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
		: "a"(leaf), "c"(subleaf)
		: "memory"
	);
#else
	(void)leaf; (void)subleaf;
	*a = *b = *c = *d = 0;
#endif
}

/* Check whether the host CPU implements the given CPUID leaf. */
static int
has_cpuid_leaf(uint32_t leaf)
{
	uint32_t max_leaf, unused;
	x86_cpuid(0, 0, &max_leaf, &unused, &unused, &unused);
	return max_leaf >= leaf;
}

/* x86_64 leaf-1 feature bits (EDX) */
#define CPUID_EDX_SSE   (1U << 25)
#define CPUID_EDX_SSE2  (1U << 26)

/* x86_64 leaf-1 feature bits (ECX) */
#define CPUID_ECX_SSE3      (1U << 0)
#define CPUID_ECX_SSSE3     (1U << 9)
#define CPUID_ECX_FMA       (1U << 12)
#define CPUID_ECX_SSE4_1    (1U << 19)
#define CPUID_ECX_SSE4_2    (1U << 20)
#define CPUID_ECX_POPCNT    (1U << 23)
#define CPUID_ECX_AES       (1U << 25)
#define CPUID_ECX_OSXSAVE   (1U << 27)
#define CPUID_ECX_AVX       (1U << 28)

/* x86_64 leaf 0x80000001 feature bits (ECX) */
#define CPUID_EXTECX_OSXSAVE  (1U << 27)
#define CPUID_EXTECX_AVX      (1U << 28)

/* Detect x86_64 CPU features via CPUID instructions.
 * Returns 0 on non-x86 hosts (where CPUID is unavailable). */
static uint64_t
detect_x86_64_features(void)
{
	uint64_t features = 0;
	uint32_t a, b, c, d;

	if (!has_cpuid_leaf(1))
		return 0;

	x86_cpuid(1, 0, &a, &b, &c, &d);

	/* EDX: baseline SSE */
	if (d & CPUID_EDX_SSE)   features |= MT_FEATURE_SSE;
	if (d & CPUID_EDX_SSE2)  features |= MT_FEATURE_SSE2;

	/* ECX: SSE3 through AES */
	if (c & CPUID_ECX_SSE3)    features |= MT_FEATURE_SSE3;
	if (c & CPUID_ECX_SSSE3)   features |= MT_FEATURE_SSSE3;
	if (c & CPUID_ECX_SSE4_1)  features |= MT_FEATURE_SSE4_1;
	if (c & CPUID_ECX_SSE4_2)  features |= MT_FEATURE_SSE4_2;
	if (c & CPUID_ECX_POPCNT)  features |= MT_FEATURE_POPCNT;
	if (c & CPUID_ECX_FMA)     features |= MT_FEATURE_FMA;

	/* AVX requires OSXSAVE + XGETBV XCR0[1:2] to confirm OS support */
	if ((c & CPUID_ECX_OSXSAVE) && (c & CPUID_ECX_AVX)) {
		uint32_t xcr0_lo, xcr0_hi;
#if defined(__x86_64__) || defined(__i386__)
		__asm__ volatile("xgetbv"
		                 : "=a"(xcr0_lo), "=d"(xcr0_hi)
		                 : "c"(0));
		if ((xcr0_lo & 6) == 6)  /* XCR0[1]=XMM, XCR0[2]=YMM */
			features |= MT_FEATURE_AVX;
#else
		(void)xcr0_lo; (void)xcr0_hi;
#endif
	}

	/* Leaf 7 (subleaf 0): extended features (AVX2, BMI, AVX-512) */
	if (has_cpuid_leaf(7)) {
		x86_cpuid(7, 0, &a, &b, &c, &d);
		if (b & (1U << 3))   features |= MT_FEATURE_BMI;      /* BMI1/BMI2 */
		if (b & (1U << 5))   features |= MT_FEATURE_AVX2;     /* AVX2 */
		if (b & (1U << 16))  features |= MT_FEATURE_AVX512F;  /* AVX-512F */
	}

	return features;
}

/* Linux /proc/cpuinfo fallback: works on any architecture, but only detects
 * features the kernel has chosen to report in the "flags" or "Features" line.
 * On x86_64 this is a slower alternative to CPUID; on other architectures it
 * is the primary (and only) detection mechanism. */
#if defined(__linux__)
static uint64_t
detect_features_from_proc_cpuinfo(void)
{
	uint64_t features = 0;
	FILE *f = fopen("/proc/cpuinfo", "r");
	if (!f)
		return 0;

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "flags", 5) == 0) {
			/* x86_64 flags line — may duplicate CPUID, but covers
			 * cases where /proc/cpuinfo is the only source. */
			if (strstr(line, " sse "))
				features |= MT_FEATURE_SSE;
			if (strstr(line, " sse2 "))
				features |= MT_FEATURE_SSE2;
			if (strstr(line, " avx "))
				features |= MT_FEATURE_AVX;
			if (strstr(line, " avx2 "))
				features |= MT_FEATURE_AVX2;
			if (strstr(line, " popcnt "))
				features |= MT_FEATURE_POPCNT;
			if (strstr(line, " avx512f "))
				features |= MT_FEATURE_AVX512F;
			/* riscv64 */
			if (strstr(line, " rv_f "))
				features |= MT_FEATURE_RV_F;
			if (strstr(line, " rv_d "))
				features |= MT_FEATURE_RV_D;
			if (strstr(line, " rv_c "))
				features |= MT_FEATURE_RV_C;
			if (strstr(line, " rv_v "))
				features |= MT_FEATURE_RV_V;
		}
		if (strncmp(line, "Features", 8) == 0) {
			/* aarch64 Features line */
			if (strstr(line, " fp "))
				features |= MT_FEATURE_VFP;
			if (strstr(line, " neon ") || strstr(line, " asimd "))
				features |= MT_FEATURE_NEON;
			if (strstr(line, " sve "))
				features |= MT_FEATURE_SVE;
		}
		/* arm: look for CPU architecture line */
		if (strncmp(line, "model name", 10) == 0 ||
		    strncmp(line, "Processor", 9) == 0) {
			if (strstr(line, "ARMv7"))
				features |= MT_FEATURE_THUMB;
		}
	}
	fclose(f);
	return features;
}
#else
static uint64_t
detect_features_from_proc_cpuinfo(void)
{
	return 0;
}
#endif

/* Public API: detect CPU features for the host architecture.
 *
 * On x86_64, uses CPUID for precise feature detection.
 * On other architectures, falls back to /proc/cpuinfo parsing (Linux).
 *
 * Returns a bitmask of MT_FEATURE_* flags, or 0 if detection failed
 * or the host has no special features beyond baseline.
 */
uint64_t
detect_cpu_features(const char *arch)
{
	/* x86_64 gets the full CPUID treatment */
	if (strcmp(arch, "x86_64") == 0) {
		uint64_t cpuid_features = detect_x86_64_features();
		if (cpuid_features != 0)
			return cpuid_features;
		/* If CPUID returned nothing (e.g. running under emulation
		 * that doesn't support it), fall through to /proc/cpuinfo. */
	}
#if defined(__linux__)
	/* All architectures: fall back to /proc/cpuinfo parsing */
	return detect_features_from_proc_cpuinfo();
#else
	(void)arch;
	return 0;
#endif
}

/* Map a -march=x86-64-v{N} level name to its feature bitmask.
 * Returns 0 if the level name is not recognised. */
uint64_t
march_x86_64_v_level(const char *value)
{
	if (!value)
		return 0;

	/* -march=native → detected live (handled by caller) */
	/* -march=x86-64    → baseline (return 0) */

	uint64_t features = 0;
	int level = -1;

	/* Accept: "x86-64-v2", "v2", "x86-64-v3", etc. */
	if (strncmp(value, "x86-64-v", 8) == 0)
		level = value[8] - '0';
	else if (value[0] == 'v' && value[1] >= '2' && value[1] <= '4' && value[2] == '\0')
		level = value[1] - '0';

	if (level < 2 || level > 4)
		return 0;

	/* Common baseline: SSE + SSE2 */
	features |= MT_FEATURE_SSE | MT_FEATURE_SSE2 | MT_FEATURE_SSE3
	          | MT_FEATURE_SSSE3 | MT_FEATURE_SSE4_1 | MT_FEATURE_SSE4_2
	          | MT_FEATURE_POPCNT;

	if (level >= 3) {
		features |= MT_FEATURE_AVX | MT_FEATURE_AVX2
		          | MT_FEATURE_BMI | MT_FEATURE_FMA;
	}
	if (level >= 4) {
		features |= MT_FEATURE_AVX512F | MT_FEATURE_BMI;
	}

	return features;
}
