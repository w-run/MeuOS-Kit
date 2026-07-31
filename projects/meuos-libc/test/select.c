/* select / fd_set 回归测试：
 * 1) fd_set 可从 <sys/types.h> 获得（glibc 兼容，meow 依赖此行为）；
 * 2) FD_ZERO/FD_SET/FD_CLR/FD_ISSET 宏语义；
 * 3) select() 可从 <unistd.h> 声明（glibc 兼容）并真实等待 pipe 就绪。 */
#include <sys/types.h>
#include <sys/select.h>
#include <stdio.h>
#include <unistd.h>

int
main(void)
{
	int p[2];
	char c;
	fd_set rfds;

	/* glibc 兼容路径：只包含 <sys/types.h> 就能用 fd_set 与 FD_*。 */
	FD_ZERO(&rfds);
	FD_SET(3, &rfds);
	if (!FD_ISSET(3, &rfds))
		return 1;
	FD_CLR(3, &rfds);
	if (FD_ISSET(3, &rfds))
		return 1;

	/* select() 从 <unistd.h> 可见，并真实阻塞在 pipe 上。 */
	if (pipe(p) != 0)
		return 1;
	if (write(p[1], "x", 1) != 1)
		return 1;
	FD_ZERO(&rfds);
	FD_SET(p[0], &rfds);
	if (select(p[0] + 1, &rfds, NULL, NULL, NULL) != 1)
		return 1;
	if (!FD_ISSET(p[0], &rfds))
		return 1;
	if (read(p[0], &c, 1) != 1 || c != 'x')
		return 1;
	close(p[0]);
	close(p[1]);
	puts("PASS select");
	return 0;
}
