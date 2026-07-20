#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int
main(void)
{
	char working_directory[256];
	char text[3];
	int file;
	int duplicate;

	if (!getcwd(working_directory, sizeof(working_directory)))
		return 1;
	/* Cleanup makes the test repeatable after an interrupted previous run. */
	rmdir("/tmp/meuos-libc-syscall-check");
	if (mkdir("/tmp/meuos-libc-syscall-check", 0700) != 0)
		return 1;
	if (chdir("/tmp/meuos-libc-syscall-check") != 0)
		return 1;
	if (chdir(working_directory) != 0)
		return 1;
	if (rmdir("/tmp/meuos-libc-syscall-check") != 0)
		return 1;
	file = open("/tmp/meuos-libc-syscall-check-file", O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (file < 0 || write(file, "OK", 2) != 2 || lseek(file, 0, SEEK_SET) != 0 || close(file) != 0)
		return 1;
	file = open("/tmp/meuos-libc-syscall-check-file", O_RDONLY);
	if (file < 0 || read(file, text, 2) != 2 || close(file) != 0 || text[0] != 'O' || text[1] != 'K')
		return 1;
	if (rename("/tmp/meuos-libc-syscall-check-file", "/tmp/meuos-libc-syscall-check-file-renamed") != 0 ||
	    unlink("/tmp/meuos-libc-syscall-check-file-renamed") != 0)
		return 1;
	duplicate = dup(1);
	if (duplicate < 0 || close(duplicate) != 0)
		return 1;
	puts("PASS filesystem syscalls");
	return 0;
}
