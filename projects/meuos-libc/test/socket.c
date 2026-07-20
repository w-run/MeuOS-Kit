#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int
main(void)
{
	const char *path = "meuos-libc-socket";
	struct sockaddr_un address;
	int server;
	int client;

	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	address.sun_path[0] = 0;
	strcpy(address.sun_path + 1, path);
	server = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server < 0)
		return 11;
	if (bind(server, (const struct sockaddr *)&address, 2 + strlen(path)) != 0) {
		/* Some restricted CI sandboxes prohibit every bind(2) invocation. */
		if (errno == EPERM) {
			puts("SKIP socket bind restricted");
			return 0;
		}
		return 12;
	}
	if (listen(server, 1) != 0)
		return 13;
	client = socket(AF_UNIX, SOCK_STREAM, 0);
	if (client < 0)
		return 14;
	if (connect(client, (const struct sockaddr *)&address, 2 + strlen(path)) != 0)
		return 15;
	if (close(client) != 0 || close(server) != 0)
		return 16;
	puts("PASS socket syscalls");
	return 0;
}
