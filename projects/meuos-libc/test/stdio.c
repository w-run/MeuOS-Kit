#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int
main(void)
{
	char word[8];
	char character;
	int decimal;
	int hexadecimal;
	int descriptors[2];
	char error_text[64];
	ssize_t error_length;
	int stdout_copy;
	char memory[] = "abc";
	char path[] = "/tmp/meuos-libc-stdio-stream";
	FILE *stream;
	if (sscanf("name -7 2a !", "%s %d %x %c", word, &decimal, &hexadecimal, &character) != 4 || strcmp(word, "name") || decimal != -7 || hexadecimal != 42 || character != '!')
		return 1;
	if (pipe(descriptors) != 0 || write(descriptors[1], "12", 2) != 2 || close(descriptors[1]) != 0 || dup2(descriptors[0], 0) < 0 || close(descriptors[0]) != 0 || scanf("%d", &decimal) != 1 || decimal != 12)
		return 1;
	errno = EBADF;
	stdout_copy = dup(1);
	if (stdout_copy < 0 || pipe(descriptors) != 0 || dup2(descriptors[1], 1) < 0 || close(descriptors[1]) != 0)
		return 1;
	perror("stdio");
	error_length = read(descriptors[0], error_text, sizeof(error_text) - 1);
	if (error_length <= 0 || close(descriptors[0]) != 0)
		return 1;
	error_text[error_length] = 0;
	if (dup2(stdout_copy, 1) < 0 || close(stdout_copy) != 0)
		return 1;
	if (strcmp(error_text, "stdio: Bad file descriptor\n") != 0)
		return 1;
	stream = fmemopen(memory, sizeof(memory), "r");
	if (!stream || getc(stream) != 'a' || getc(stream) != 'b'
	 || ungetc('B', stream) != 'B' || getc(stream) != 'B'
	 || getc(stream) != 'c' || getc(stream) != 0 || getc(stream) != EOF
	 || fclose(stream) != 0)
		return 1;
	if ((descriptors[0] = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0
	 || write(descriptors[0], "line\n", 5) != 5 || close(descriptors[0]) != 0)
		return 1;
	stream = fopen(path, "r");
	if (!stream || !fgets(error_text, sizeof(error_text), stream)
	 || strcmp(error_text, "line\n") != 0 || fclose(stream) != 0
	 || unlink(path) != 0)
		return 1;
	if (snprintf(error_text, sizeof(error_text), "%03o %zu %ld", 7u,
		(size_t)9, -2L) != 8 || strcmp(error_text, "007 9 -2") != 0)
		return 1;
	if (snprintf(error_text, sizeof(error_text), "%.*s", 3, "abcdef") != 3
	 || strcmp(error_text, "abc") != 0)
		return 1;
	if (pipe(descriptors) != 0 || !(stream = fdopen(descriptors[1], "w"))
	 || fprintf(stream, "%s:%d", "stream", 7) != 8 || fclose(stream) != 0)
		return 1;
	error_length = read(descriptors[0], error_text, sizeof(error_text) - 1);
	if (error_length != 8 || close(descriptors[0]) != 0)
		return 1;
	error_text[error_length] = 0;
	if (strcmp(error_text, "stream:7") != 0)
		return 1;
	stream = popen("echo popen-ok", "r");
	if (!stream || !fgets(error_text, sizeof(error_text), stream)
	 || strcmp(error_text, "popen-ok\n") != 0 || pclose(stream) != 0)
		return 1;
	return printf("%s %d %x %c %p\n", "PASS", -7, 42, '!', (void *)0) < 0;
}
