#include "mbox/http.h"
#include "mbox/pty.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

/* ── WebPTY bridge ────────────────────────────────────────────────────── */

/* Run the WebPTY server: accepts one client, bridges WebSocket ↔ PTY.
 * Returns 0 on normal exit. */
int webpty_run(http_server_t *http, pty_muxer_t *pty) {
    /* Accept one connection */
    if (http_accept(http) < 0)
        return -1;

    /* Read the HTTP request (GET or WebSocket Upgrade) */
    char reqbuf[8192];
    if (http_read_request(http, reqbuf, (int)sizeof(reqbuf)) < 0) {
        http_close(http);
        return -1;
    }

    /* If WebSocket upgrade succeeded, bridge data */
    if (http->is_websocket) {
        char buf[65536];
                struct pollfd pfds[2];
        int running = 1;

        while (running) {
            pfds[0].fd = pty->master_fd;
            pfds[0].events = POLLIN;
            pfds[0].revents = 0;
            pfds[1].fd = http->client_fd;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;

            int ret = poll(pfds, 2, 500);  /* 500ms timeout */
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }

            /* ── PTY → WebSocket ─────────────────────────────────── */
            if (pfds[0].revents & POLLIN) {
                int n = pty_read(pty, buf, (int)sizeof(buf));
                if (n > 0) {
                    ws_send(http, WS_OPCODE_TEXT, buf, n);
                } else if (n == 0) {
                    /* PTY child exited */
                    ws_send(http, WS_OPCODE_TEXT, "[process exited]\n", 17);
                    running = 0;
                }
            }

            /* ── WebSocket → PTY ─────────────────────────────────── */
            if (pfds[1].revents & POLLIN) {
                int n = ws_recv(http, buf, (int)sizeof(buf));
                if (n > 0) {
                    /* Parse JSON key event and inject */
                    /* We look for "key":"<value>" in the JSON */
                    const char *key_marker = strstr(buf, "\"key\":\"");
                    if (key_marker) {
                        key_marker += 7;  /* skip "key":" */
                        char key_val[64];
                        int ki = 0;
                        while (*key_marker && *key_marker != '"' && ki < 63)
                            key_val[ki++] = *key_marker++;
                        key_val[ki] = '\0';
                        pty_inject_key(pty, "keydown", key_val);
                    }
                } else if (n == 0) {
                    running = 0;  /* WebSocket closed */
                }
            }

            /* ── PTY error ────────────────────────────────────────── */
            if (pfds[0].revents & (POLLERR | POLLHUP)) {
                running = 0;
            }
        }
    }

    http_close(http);
    return 0;
}