/* termios/termios.c — POSIX terminal I/O functions */

#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>

int
tcgetattr(int fd, struct termios *t)
{
	return ioctl(fd, TCGETS, t);
}

int
tcsetattr(int fd, int action, const struct termios *t)
{
	switch (action) {
	case TCSANOW:   return ioctl(fd, TCSETS, t);
	case TCSADRAIN: return ioctl(fd, TCSETSW, t);
	case TCSAFLUSH: return ioctl(fd, TCSETSF, t);
	default:
		errno = EINVAL;
		return -1;
	}
}

int
tcsendbreak(int fd, int duration)
{
	return ioctl(fd, TCSBRKP, duration);
}

int
tcdrain(int fd)
{
	return ioctl(fd, TCSBRK, 1);
}

int
tcflush(int fd, int queue)
{
	return ioctl(fd, TCFLSH, queue);
}

int
tcflow(int fd, int action)
{
	return ioctl(fd, TCXONC, action);
}

speed_t
cfgetospeed(const struct termios *t)
{
	return t->c_cflag & CBAUD;
}

speed_t
cfgetispeed(const struct termios *t)
{
	return t->c_ispeed;
}

int
cfsetospeed(struct termios *t, speed_t speed)
{
	t->c_cflag = (t->c_cflag & ~CBAUD) | (speed & CBAUD);
	t->c_ospeed = speed;
	return 0;
}

int
cfsetispeed(struct termios *t, speed_t speed)
{
	t->c_ispeed = speed;
	return 0;
}

void
cfmakeraw(struct termios *t)
{
	t->c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
	t->c_oflag &= ~OPOST;
	t->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
	t->c_cflag &= ~(CSIZE|PARENB);
	t->c_cflag |= CS8;
	t->c_cc[VMIN]  = 1;
	t->c_cc[VTIME] = 0;
}
