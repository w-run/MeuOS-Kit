#include "ir.h"

/* Post-register-allocation optimization pass.
 *
 * Runs after rega() to eliminate redundant slot-to-slot copies
 * (Ocopy instructions where both operands are RSlot).
 *
 * Algorithm: scan each block forward, tracking the last Ocopy
 * writer per slot.  If a slot is written by an Ocopy, and a
 * *different* slot-write Ocopy (anything that assigns to the
 * same slot again) appears later with no intervening READ of
 * dst, the first Ocopy is dead and can be removed.
 *
 * Only memory-touching instructions (Ostore*, Ocall, Oload,
 * or any ins with RSlot operands) reset the tracking.  Pure
 * register ALU (add/sub/xor/...) does not touch stack slots
 * and is ignored.
 */

/* Return nonzero if instruction `i` can read from a stack slot.
 * This means it uses an RSlot or RMem operand. */
static int
reads_slot(Ins *i)
{
	return rtype(i->arg[0]) == RSlot
	    || rtype(i->arg[0]) == RMem
	    || rtype(i->arg[1]) == RSlot
	    || rtype(i->arg[1]) == RMem;
}

/* Return nonzero if instruction `i` can write to a stack slot.
 * This includes Ostore* and Ocopy with RSlot/to. */
static int
writes_slot(Ins *i)
{
	return (i->op == Ostorew || i->op == Ostorel
	     || i->op == Ostoreh || i->op == Ostoreb
	     || i->op == Ostores || i->op == Ostored)
	    || (i->op == Ocopy && rtype(i->to) == RSlot);
}

void
postra(Fn *fn)
{
	Blk *b;
	Ins *i;
	uint n;
	int t;
	int nslots;
	int killed = 0;

	nslots = fn->slot;
	if (nslots <= 0)
		return;

	if (debug['P'])
		fprintf(stderr, "postra: function %s, %d slots\n",
			fn->name, nslots);

	/* Count and log slot-to-slot Ocopy instructions */
	if (debug['P']) {
		int slot_copy_count = 0;
		int slot_copy_blocks = 0;
		for (b = fn->start; b; b = b->link) {
			int block_count = 0;
			for (n = 0; n < b->nins; n++) {
				i = &b->ins[n];
				if (i->op == Ocopy
				&& rtype(i->to) == RSlot
				&& (rtype(i->arg[0]) == RSlot
				 || rtype(i->arg[0]) == RMem)) {
					block_count++;
				}
			}
			if (block_count > 0) {
				slot_copy_blocks++;
				slot_copy_count += block_count;
				fprintf(stderr, "postra: block %s has %d "
					"slot-to-slot copies\n",
					b->name, block_count);
			}
		}
		fprintf(stderr, "postra: total %d slot-to-slot copies "
			"in %d blocks\n",
			slot_copy_count, slot_copy_blocks);
	}

	for (b = fn->start; b; b = b->link) {
		/* last_def[slot] = index of last Ocopy writing this slot,
		 * or -1 if unknown. */
		int *last_def = emalloc(nslots * sizeof(int));
		/* last_def_src[slot] = source slot if Ocopy, or -2 if
		 * non-Ocopy writer, or -1 if unknown. */
		int *last_def_src = emalloc(nslots * sizeof(int));
		for (t = 0; t < nslots; t++) {
			last_def[t] = -1;
			last_def_src[t] = -1;
		}

		for (n = 0; n < b->nins; n++) {
			i = &b->ins[n];

			if (i->op == Ocopy && rtype(i->to) == RSlot) {
				int dst = rsval(i->to);

				if (dst >= 0 && dst < nslots
				&& (rtype(i->arg[0]) == RSlot
				 || rtype(i->arg[0]) == RMem
				 || rtype(i->arg[0]) == RTmp)) {
					int src = (rtype(i->arg[0]) == RSlot)
						? rsval(i->arg[0]) : -1;

					/* If we have a previous writer to this
					 * slot and it's ALSO an Ocopy, and dst
					 * hasn't been read since, kill the prev. */
					if (last_def[dst] >= 0
					&& last_def_src[dst] >= 0) {
						int prev = last_def[dst];
						int should_kill = 1;
						uint k;

						for (k = prev + 1; k < n; k++) {
							Ins *ck = &b->ins[k];
							/* Read of dst kills: any use
							 * of dst slot as operand. */
							if (rtype(ck->arg[0]) == RSlot
							&& rsval(ck->arg[0]) == dst)
								{ should_kill = 0; break; }
							if (rtype(ck->arg[1]) == RSlot
							&& rsval(ck->arg[1]) == dst)
								{ should_kill = 0; break; }
							/* Also, a write to dst by a
							 * non-Ocopy instruction is a
							 * different path.  But if
							 * it's an Ocopy to dst, that's
							 * handled by this iteration
							 * itself (we're at n).
							 * Non-Ocopy write already
							 * cleared last_def[dst]. */
						}

						if (should_kill) {
							b->ins[prev].op = Onop;
						killed++;
							if (debug['P'])
								fprintf(stderr,
									"postra: kill "
									"Ocopy %s[%d]\n",
									b->name, prev);
						}
					}

					/* Track this Ocopy */
					last_def[dst] = (int)n;
					last_def_src[dst] = src;
					continue;
				}
			}

			/* Non-Ocopy.  Only reset slot tracking if
			 * this instruction actually touches memory. */
			if (writes_slot(i)) {
				/* For Ostore*, the dest slot is
				 * arg[1].  Reset only that slot. */
				if (i->op >= Ostorew && i->op <= Ostored
				&& rtype(i->arg[1]) == RSlot) {
					int sd = rsval(i->arg[1]);
					if (sd >= 0 && sd < nslots) {
						last_def[sd] = -1;
						last_def_src[sd] = -1;
					}
					/* Also reset source if it's a slot */
					if (rtype(i->arg[0]) == RSlot) {
						int ss = rsval(i->arg[0]);
						if (ss >= 0 && ss < nslots) {
							last_def[ss] = -1;
							last_def_src[ss] = -1;
						}
					}
				}
				/* For Ocall, reset all (callee may
				 * clobber stack slots). */
				if (rtype(i->arg[1]) == RCall) {
					for (t = 0; t < nslots; t++) {
						last_def[t] = -1;
						last_def_src[t] = -1;
					}
				}
				/* For any other memory write (Ocopy
				 * with RMem dest, etc.), reset the
				 * dest slot. */
				if (i->op == Ocopy && rtype(i->to) == RMem
				   && rtype(i->to) != RSlot) {
					/* Memory address unknown, reset all */
					for (t = 0; t < nslots; t++) {
						last_def[t] = -1;
						last_def_src[t] = -1;
					}
				}
			}

			/* If this instruction reads from an RSlot,
			 * reset tracking for that slot (the value
			 * is consumed and may be invalidated). */
			if (reads_slot(i)) {
				/* Only reset tracking for the slot
				 * that was read, since a read doesn't
				 * modify it. Actually, don't reset
				 * on read — we track definitions, not
				 * values.  Reads are only checked in
				 * the kill analysis above. */
			}
		}

		free(last_def);
		free(last_def_src);
	}

	if (debug['P'])
		fprintf(stderr, "postra: killed %d copies total\n", killed);
}
