#ifndef MBOX_MCP_H
#define MBOX_MCP_H

#include "mbox/pty.h"
#include "mbox/ns.h"

/* MCP server context */
typedef struct {
    pty_muxer_t *pty;       /* shared PTY muxer */
    const char *rootfs;     /* rootfs path (for file operations) */
} mcp_ctx_t;

/* Initialize MCP context */
void mcp_init(mcp_ctx_t *ctx, pty_muxer_t *pty, const char *rootfs);

/* Run the MCP server over stdio (JSON-RPC style).
 * Reads JSON lines from stdin, processes requests, writes responses to stdout.
 * Blocks until EOF on stdin.
 * Returns 0 on normal shutdown. */
int mcp_run_stdio(mcp_ctx_t *ctx);

/* Run the MCP server on a Unix socket at the given path.
 * Listens for one connection, processes requests, then returns.
 * Returns 0 on success. */
int mcp_run_unix(mcp_ctx_t *ctx, const char *socket_path);

/* Handle a single MCP request (JSON string).
 * Writes response to out buffer.
 * Returns 0 on success, -1 on parse error. */
int mcp_handle_request(mcp_ctx_t *ctx, const char *request,
                       char *out, int out_len);

#endif /* MBOX_MCP_H */