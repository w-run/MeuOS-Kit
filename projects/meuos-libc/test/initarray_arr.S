/* initarray_arr.S — .init_array / .fini_array entries for initarray.c.
 *
 * Written in assembly because mcc parses __attribute__((constructor)) but
 * does not yet emit an .init_array entry for it.  `.dc.a` emits one
 * address-sized value, so the same source serves all six architectures.
 *
 * Order matters: .init_array is walked front to back, .fini_array back to
 * front, so the gate expects C1, C2, main, D2, D1.
 */

.section .init_array,"aw"
	.balign 8
	.dc.a ctor_first
	.dc.a ctor_second

.section .fini_array,"aw"
	.balign 8
	.dc.a dtor_first
	.dc.a dtor_second

.section .note.GNU-stack,"",%progbits
