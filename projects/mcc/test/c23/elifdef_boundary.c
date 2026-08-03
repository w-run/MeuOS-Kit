/* elifdef_boundary.c — C23 #elifdef/#elifndef inside SKIPPED groups.
 *
 * Regression for the D3-PP defect: while skipping an inactive group,
 * skipbody() handled #elifdef/#elifndef by returning as soon as the
 * condition was true, leaving `tok` parked on the macro-name identifier
 * instead of the terminating newline.  The caller (directive()) requires
 * tok == TNEWLINE, so the leftover identifier surfaced as a bogus
 * "expected newline after preprocessing directive" error on perfectly
 * valid code.  Every group below must be resolved silently.
 */

int main(void) {
	int result = 0;

	/* A nested #if..#elifdef..#endif buried inside a dead `#if 0`
	 * group.  The whole group is skipped wholesale by depth counting;
	 * the inner #elifdef must never be evaluated nor diagnosed. */
#if 0
#if 1
	result += 1000;
#elifdef UNDEFINED_MACRO
	result += 2000;
#endif
	result += 4000;
#else
	result += 1;
#endif
	/* result == 1 */

	/* #elifdef at depth 0 of a dead group, macro undefined: the branch
	 * stays untaken and control must fall through to #else. */
#if 0
	result += 1000;
#elifdef STILL_UNDEFINED
	result += 2000;
#else
	result += 2;
#endif
	/* result == 3 */

	/* #elifdef at depth 0 of a dead group whose macro IS defined: this
	 * is the exact path that used to return early with `tok` on the
	 * macro name.  The branch is taken and must be entered cleanly. */
#define D3_DEFINED 1
#if 0
	result += 1000;
#elifdef D3_DEFINED
	result += 4;
#else
	result += 2000;
#endif
	/* result == 7 */

	/* Same early-return path via #elifndef. */
#if 0
	result += 1000;
#elifndef D3_ABSENT
	result += 8;
#else
	result += 2000;
#endif
	/* result == 15 */

	/* A chain of untaken #elifdef/#elifndef groups: each one must
	 * consume its whole directive line before moving to the next. */
#if 0
	result += 1000;
#elifdef D3_NOPE_1
	result += 2000;
#elifdef D3_NOPE_2
	result += 4000;
#elifndef D3_DEFINED
	result += 8000;
#else
	result += 16;
#endif
	/* result == 31 */

	/* Deeply nested dead groups, each level carrying #elifdef. */
#if 0
#if 0
#if 1
#elifdef D3_A
#endif
#elifdef D3_B
#endif
#elifdef D3_C
	result += 1000;
#else
	result += 32;
#endif
	/* result == 63 */

	return result == 63 ? 0 : 1;
}
