#ifndef MBOX_WEBPTY_H
#define MBOX_WEBPTY_H

#include "mbox/http.h"
#include "mbox/pty.h"

/* Run the WebPTY server: HTTP + WebSocket ↔ PTY bridge.
 * Listens on the http server's port, accepts one connection,
 * bridges WebSocket data to PTY input and PTY output to WebSocket.
 * Returns 0 on normal exit. */
int webpty_run(http_server_t *http, pty_muxer_t *pty);

#endif /* MBOX_WEBPTY_H */