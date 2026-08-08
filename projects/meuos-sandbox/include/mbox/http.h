#ifndef MBOX_HTTP_H
#define MBOX_HTTP_H

#include <stddef.h>

/* WebSocket opcodes */
#define WS_OPCODE_CONTINUE  0x0
#define WS_OPCODE_TEXT      0x1
#define WS_OPCODE_BINARY    0x2
#define WS_OPCODE_CLOSE     0x8
#define WS_OPCODE_PING      0x9
#define WS_OPCODE_PONG      0xA

/* HTTP server state */
typedef struct {
    int listen_fd;           /* server socket fd (-1 if not listening) */
    int client_fd;           /* current client fd (-1 if none) */
    int port;                /* listening port */
    int is_websocket;        /* 1 if client upgraded to WebSocket */
} http_server_t;

/* Initialize HTTP server state */
void http_init(http_server_t *srv);

/* Listen on the given port.
 * Returns 0 on success, -1 on error. */
int http_listen(http_server_t *srv, int port);

/* Accept one connection (blocking).
 * Returns client fd, or -1 on error. */
int http_accept(http_server_t *srv);

/* Read HTTP request from client.
 * Returns 0 on success (request read into buf), -1 on error.
 * On WebSocket Upgrade, sets srv->is_websocket and performs handshake. */
int http_read_request(http_server_t *srv, char *buf, int buf_len);

/* Send HTTP response (static content).
 * Returns number of bytes sent, or -1 on error. */
int http_send_response(http_server_t *srv, int status,
                       const char *content_type,
                       const char *body, int body_len);

/* Perform WebSocket upgrade handshake.
 * Returns 0 on success, -1 on error. */
int ws_handshake(http_server_t *srv, const char *key);

/* Send a WebSocket frame.
 * Returns bytes sent, or -1 on error. */
int ws_send(http_server_t *srv, int opcode, const char *data, int len);

/* Read a WebSocket frame.
 * Returns payload length, 0 on close, -1 on error.
 * Payload is written into buf. */
int ws_recv(http_server_t *srv, char *buf, int buf_len);

/* Close HTTP/WS connection */
void http_close(http_server_t *srv);

#endif /* MBOX_HTTP_H */