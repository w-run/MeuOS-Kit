#include "mbox/pty.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <errno.h>

void pty_init(pty_muxer_t *pm) {
    memset(pm, 0, sizeof(*pm));
    pm->master_fd = -1;
    pm->slave_fd = -1;
}

static void screen_append(pty_screen_t *scr, const char *data, int len) {
    for (int i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            /* finalize current line */
            int tail = scr->head;
            if (scr->count < PTY_SCREEN_MAX_LINES)
                scr->count++;
            else
                scr->tail = (scr->tail + 1) % PTY_SCREEN_MAX_LINES;
            scr->head = (scr->head + 1) % PTY_SCREEN_MAX_LINES;
            scr->lines[tail][0] = '\0';
        } else if (c == '\r') {
            /* ignore carriage return for line tracking */
        } else if (c >= 32) {
            int tail = scr->head;
            int pos = (int)strlen(scr->lines[tail]);
            if (pos < PTY_SCREEN_MAX_LINE_LEN - 1) {
                scr->lines[tail][pos] = c;
                scr->lines[tail][pos + 1] = '\0';
            }
        }
    }
}

int pty_open(pty_muxer_t *pm, char *const argv[], char *const envp[]) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        fprintf(stderr, "pty: posix_openpt: %s\n", strerror(errno));
        return -1;
    }

    if (grantpt(master) < 0) {
        fprintf(stderr, "pty: grantpt: %s\n", strerror(errno));
        close(master);
        return -1;
    }

    if (unlockpt(master) < 0) {
        fprintf(stderr, "pty: unlockpt: %s\n", strerror(errno));
        close(master);
        return -1;
    }

    const char *slave_name = ptsname(master);
    if (!slave_name) {
        fprintf(stderr, "pty: ptsname: %s\n", strerror(errno));
        close(master);
        return -1;
    }

    int pid = fork();
    if (pid < 0) {
        fprintf(stderr, "pty: fork: %s\n", strerror(errno));
        close(master);
        return -1;
    }

    if (pid == 0) {
        /* Child: connect PTY slave to stdin/stdout/stderr */
        close(master);

        int slave = open(slave_name, O_RDWR);
        if (slave < 0)
            _exit(127);

        /* Create new session */
        setsid();

        /* Set controlling terminal */
        if (ioctl(slave, TIOCSCTTY, 0) < 0) {
            /* non-fatal */
        }

        /* Duplicate PTY slave to stdin/stdout/stderr */
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);

        if (slave > 2)
            close(slave);

        /* Set terminal attributes to raw mode */
        struct termios tio;
        tcgetattr(0, &tio);
        cfmakeraw(&tio);
        tcsetattr(0, TCSANOW, &tio);

        execve(argv[0], argv, envp);
        _exit(127);
    }

    /* Parent: store PTY master fd and child pid */
    close(pm->master_fd); /* close any previous */
    pm->master_fd = master;
    pm->child_pid = pid;
    pm->slave_fd = -1;  /* not used in parent */

    return 0;
}

void pty_close(pty_muxer_t *pm) {
    if (pm->child_pid > 0) {
        kill(pm->child_pid, SIGTERM);
        /* Give it a moment, then SIGKILL */
        usleep(50000);
        kill(pm->child_pid, SIGKILL);
        waitpid(pm->child_pid, NULL, WNOHANG);
        pm->child_pid = 0;
    }
    if (pm->master_fd >= 0) {
        close(pm->master_fd);
        pm->master_fd = -1;
    }
}

int pty_write(pty_muxer_t *pm, const char *buf, int len) {
    if (pm->master_fd < 0) return -1;
    int n = (int)write(pm->master_fd, buf, (size_t)len);
    if (n < 0 && errno == EAGAIN) return 0;
    return n;
}

int pty_read(pty_muxer_t *pm, char *buf, int len) {
    if (pm->master_fd < 0) return -1;
    int n = (int)read(pm->master_fd, buf, (size_t)len);
    if (n > 0) {
        screen_append(&pm->screen, buf, n);
    }
    return n;
}

static const char *key_to_escape(const char *key) {
    if (strcmp(key, "Enter") == 0)      return "\r";
    if (strcmp(key, "Escape") == 0)     return "\033";
    if (strcmp(key, "Tab") == 0)        return "\t";
    if (strcmp(key, "Backspace") == 0)  return "\177";
    if (strcmp(key, "Up") == 0)         return "\033[A";
    if (strcmp(key, "Down") == 0)       return "\033[B";
    if (strcmp(key, "Right") == 0)      return "\033[C";
    if (strcmp(key, "Left") == 0)       return "\033[D";
    if (strcmp(key, "Home") == 0)       return "\033[H";
    if (strcmp(key, "End") == 0)        return "\033[F";
    if (strcmp(key, "Delete") == 0)     return "\033[3~";
    if (strcmp(key, "PageUp") == 0)     return "\033[5~";
    if (strcmp(key, "PageDown") == 0)   return "\033[6~";
    if (strcmp(key, "Insert") == 0)     return "\033[2~";
    /* Ctrl+letter: "C-a", "C-b" etc */
    if (key[0] == 'C' && key[1] == '-' && key[2] >= 'a' && key[2] <= 'z' && key[3] == '\0') {
        static char ctrl[2];
        ctrl[0] = (char)(key[2] - 'a' + 1);
        ctrl[1] = '\0';
        return ctrl;
    }
    /* Single character keys: "a".."z", "0".."9", " " etc */
    if (key[0] && key[1] == '\0') {
        static char ch[2];
        ch[0] = key[0];
        ch[1] = '\0';
        return ch;
    }
    return NULL;
}

int pty_inject_key(pty_muxer_t *pm, const char *type, const char *key) {
    (void)type;
    const char *seq = key_to_escape(key);
    if (!seq) return -1;
    int len = (int)strlen(seq);
    return pty_write(pm, seq, len);
}

int pty_inject_mouse(pty_muxer_t *pm, const char *type, int x, int y, int btn) {
    /* XTerm mouse tracking: \033[M<btn+32><x+33><y+33> */
    /* type is not critical; we treat as click */
    (void)type;
    char seq[8];
    int cb = (btn & 3) + 32;  /* button code */
    int cx = x + 33;
    int cy = y + 33;
    /* clamp to valid range */
    if (cx < 33) cx = 33;
    if (cx > 255) cx = 255;
    if (cy < 33) cy = 33;
    if (cy > 255) cy = 255;
    snprintf(seq, sizeof(seq), "\033[M%c%c%c", cb, cx, cy);
    return pty_write(pm, seq, 6);
}

const char *pty_screen_get(pty_muxer_t *pm) {
    static char buffer[PTY_SCREEN_MAX_LINES * (PTY_SCREEN_MAX_LINE_LEN + 1) + 1];
    buffer[0] = '\0';
    int pos = 0;

    pty_screen_t *scr = &pm->screen;

    for (int i = 0; i < scr->count; i++) {
        int idx = (scr->tail + i) % PTY_SCREEN_MAX_LINES;
        pos += snprintf(buffer + pos, sizeof(buffer) - (size_t)pos,
                        "%s\n", scr->lines[idx]);
        if ((size_t)pos >= sizeof(buffer) - 1)
            break;
    }

    return buffer;
}