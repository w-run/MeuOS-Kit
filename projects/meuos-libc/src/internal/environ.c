/* environ definition for the dynamic library.
 *
 * In the static build, `char **environ` is provided by crt1.o (each arch's
 * crt1.S defines an 8-byte data slot and copies %rdx/envp into it on entry).
 * A shared libc cannot rely on a CRT object it is not linked against, so
 * libc-meuos.so defines the slot itself here.  The executable's crt1 then
 * writes its envp into this slot via the normal GOT/relocation path.
 *
 * This object is added only to the -fPIC object set that builds
 * libc-meuos.so (kept out of the static libc-meuos.a to avoid clashing with
 * the crt1.o definition there).
 */
char **environ;
