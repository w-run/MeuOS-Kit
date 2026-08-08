#ifndef MBOX_PTY_H
#define MBOX_PTY_H

#include <stddef.h>

/* Maximum number of lines in screen buffer */
#define PTY_SCREEN_MAX_LINES 1024
#define PTY_SCREEN_MAX_LINE_LEN 256

/* Screen buffer: ring of lines */
typedef struct {
    char lines[PTY_SCREEN_MAX_LINES][PTY_SCREEN_MAX_LINE_LEN];
    int  count;      /* number of valid lines */
    int  head;       /* next write position */
    int  tail;       /* oldest valid line */
} pty_screen_t;

/* PTY Muxer state */
typedef struct {
    int master_fd;          /* PTY master fd (-1 if closed) */
    int slave_fd;           /* PTY slave fd (-1 if closed) */
    int child_pid;          /* child process pid (0 if none) */
    pty_screen_t screen;    /* screen line buffer */
} pty_muxer_t;

/* Initialize PTY muxer */
void pty_init(pty_muxer_t *pm);

/* Open a new PTY with the given command (argv-style).
 * Returns 0 on success, -1 on error.
 * After success, pm->master_fd, pm->child_pid are set.
 * The child runs the command with stdin/stdout/stderr connected to the PTY. */
int pty_open(pty_muxer_t *pm, char *const argv[], char *const envp[]);

/* Close the PTY and kill the child */
void pty_close(pty_muxer_t *pm);

/* Write data to the PTY master (sends to child's stdin).
 * Returns number of bytes written, or -1 on error. */
int pty_write(pty_muxer_t *pm, const char *buf, int len);

/* Read data from the PTY master (receives child's stdout).
 * Reads up to len bytes into buf.
 * Also appends to screen buffer.
 * Returns number of bytes read, or -1 on error, or 0 on EOF. */
int pty_read(pty_muxer_t *pm, char *buf, int len);

/* Inject a keyboard event (ANSI/terminal escape sequence).
 * type: "keydown" or "keyup"
 * key: key name/char (e.g. "Enter", "a", "Escape") */
int pty_inject_key(pty_muxer_t *pm, const char *type, const char *key);

/* Inject a mouse event (ANSI escape sequence).
 * type: "mousedown", "mouseup", "mousemove"
 * x, y: coordinates (0-based)
 * btn: 0=left, 1=middle, 2=right */
int pty_inject_mouse(pty_muxer_t *pm, const char *type, int x, int y, int btn);

/* Get current screen content as a single string.
 * Returns a pointer to a static buffer (valid until next call). */
const char *pty_screen_get(pty_muxer_t *pm);

#endif /* MBOX_PTY_H */