#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int
main(void)
{
	const char *file_name = "/tmp/meuos-libc-kernel-file";
	const char *hard_name = "/tmp/meuos-libc-kernel-hard";
	const char *symbolic_name = "/tmp/meuos-libc-kernel-symbolic";
	struct stat information;
	struct timespec before;
	struct timespec after;
	struct timespec pause;
	char target[64];
	char directory_data[1024];
	char *mapping;
	int descriptor;

	unlink(symbolic_name);
	unlink(hard_name);
	unlink(file_name);
	descriptor = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (descriptor < 0 || write(descriptor, "data", 4) != 4 || close(descriptor) != 0)
		return 1;
	if (stat(file_name, &information) != 0 || !S_ISREG(information.st_mode) || information.st_size != 4)
		return 1;
	if (chmod(file_name, 0600) != 0 || access(file_name, R_OK) != 0)
		return 1;
	descriptor = open(file_name, O_RDONLY);
	if (descriptor < 0 || fstat(descriptor, &information) != 0 || information.st_size != 4 || close(descriptor) != 0)
		return 1;
	if (link(file_name, hard_name) != 0 || symlink(file_name, symbolic_name) != 0)
		return 1;
	if (lstat(symbolic_name, &information) != 0 || !S_ISLNK(information.st_mode))
		return 1;
	if (readlink(symbolic_name, target, sizeof(target)) != 27 || target[0] != '/')
		return 1;
	descriptor = open("/tmp", O_RDONLY);
	if (descriptor < 0 || getdents64(descriptor, directory_data, sizeof(directory_data)) <= 0 || close(descriptor) != 0)
		return 1;
	mapping = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return 1;
	mapping[0] = 'M';
	if (mapping[0] != 'M' || munmap(mapping, 4096) != 0)
		return 1;
	if (clock_gettime(CLOCK_MONOTONIC, &before) != 0)
		return 1;
	pause.tv_sec = 0;
	pause.tv_nsec = 1000000;
	if (nanosleep(&pause, 0) != 0 || clock_gettime(CLOCK_MONOTONIC, &after) != 0 ||
	    (after.tv_sec == before.tv_sec && after.tv_nsec < before.tv_nsec))
		return 1;
	unlink(symbolic_name);
	unlink(hard_name);
	unlink(file_name);
	puts("PASS kernel interfaces");
	return 0;
}
