/* Companion translation unit for commonsym.c.
 *
 * commonsym.c exercises C's common-symbol (tentative definition)
 * merging across translation units: `int common_ext2;` there is a
 * tentative definition that must merge with the strong definition
 * provided here, so commonsym.c's ASSERT(3, common_ext2) holds.
 */
int common_ext2 = 3;
