#include "mbox/mcp.h"
#include "mbox/pty.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <glob.h>
#include <signal.h>
#include <sys/wait.h>

void mcp_init(mcp_ctx_t *ctx, pty_muxer_t *pty, const char *rootfs) {
    ctx->pty = pty;
    ctx->rootfs = rootfs;
}

/* ── JSON utilities ───────────────────────────────────────────────────── */

/* Build a JSON string response (result). out must be large enough. */

/* Build a JSON string response with a string value. */
static void json_result_str(int id, const char *key, const char *val,
                            char *out, int out_len) {
    /* Escape JSON special characters in val */
    char escaped[65536];
    escaped[0] = '\0';
    int pos = 0;
    for (const char *p = val; *p && pos < (int)sizeof(escaped) - 8; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '\"': escaped[pos++] = '\\'; escaped[pos++] = '\"'; break;
        case '\\': escaped[pos++] = '\\'; escaped[pos++] = '\\'; break;
        case '\n': escaped[pos++] = '\\'; escaped[pos++] = 'n'; break;
        case '\r': escaped[pos++] = '\\'; escaped[pos++] = 'r'; break;
        case '\t': escaped[pos++] = '\\'; escaped[pos++] = 't'; break;
        default:
            if (c < 32) {
                pos += snprintf(escaped + pos, sizeof(escaped) - (size_t)pos, "\\u%04x", c);
            } else {
                escaped[pos++] = (char)c;
            }
            break;
        }
        if (pos >= (int)sizeof(escaped) - 8) break;
    }
    escaped[pos] = '\0';

    snprintf(out, (size_t)out_len,
             "{\"id\":%d,\"result\":{\"%s\":\"%s\"}}\n", id, key, escaped);
}

static void json_result_int(int id, const char *key, int val,
                            char *out, int out_len) {
    snprintf(out, (size_t)out_len,
             "{\"id\":%d,\"result\":{\"%s\":%d}}\n", id, key, val);
}

static void json_error(int id, const char *msg, char *out, int out_len) {
    char escaped[1024];
    int pos = 0;
    for (const char *p = msg; *p && pos < (int)sizeof(escaped) - 4; p++) {
        if (*p == '\"') { escaped[pos++] = '\\'; escaped[pos++] = '\"'; }
        else if (*p == '\\') { escaped[pos++] = '\\'; escaped[pos++] = '\\'; }
        else escaped[pos++] = *p;
    }
    escaped[pos] = '\0';
    snprintf(out, (size_t)out_len,
             "{\"id\":%d,\"error\":\"%s\"}\n", id, escaped);
}

/* Simple JSON field extractor. Returns pointer to value string (to be copied),
 * or NULL if field not found. Handles string, number, and null values. */
static const char *json_get_string(const char *json, const char *field) {
    /* Find "field": */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    /* skip whitespace and : */
    while (*p && (isspace((unsigned char)*p) || *p == ':')) p++;
    if (!*p) return NULL;

    if (*p == '\"') {
        /* String value */
        p++; /* skip opening quote */
        return p;  /* caller must find closing quote */
    }
    if (*p == 't' && strncmp(p, "true", 4) == 0) return "true";
    if (*p == 'f' && strncmp(p, "false", 5) == 0) return "false";
    if (*p == 'n' && strncmp(p, "null", 4) == 0) return "null";
    /* Must be a number */
    return p;
}

/* Copy a JSON string value from p until closing quote. Returns length. */
static int json_copy_string(const char *p, char *out, int max) {
    int i = 0;
    while (*p && *p != '\"' && i < max - 1) {
        if (*p == '\\' && *(p+1)) {
            p++;
            switch (*p) {
            case 'n': out[i++] = '\n'; break;
            case 't': out[i++] = '\t'; break;
            case 'r': out[i++] = '\r'; break;
            case '\\': out[i++] = '\\'; break;
            case '\"': out[i++] = '\"'; break;
            default: out[i++] = *p; break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return i;
}

/* ── Tool implementations ─────────────────────────────────────────────── */

/* sh: execute a command inside the sandbox */
static void tool_sh(mcp_ctx_t *ctx_ __attribute__((unused)), const char *params, int id,
                    char *out, int out_len) {
    const char *cmd_str = json_get_string(params, "cmd");
    if (!cmd_str) {
        json_error(id, "missing 'cmd' parameter", out, out_len);
        return;
    }
    char cmd[8192];
    json_copy_string(cmd_str, cmd, sizeof(cmd));

    /* Execute command via popen */
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        json_error(id, strerror(errno), out, out_len);
        return;
    }

    char result[65536];
    int pos = 0;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp) && pos < (int)sizeof(result) - (int)sizeof(buf))
        pos += snprintf(result + pos, sizeof(result) - (size_t)pos, "%s", buf);

    int exitcode = pclose(fp);

    snprintf(out, (size_t)out_len,
             "{\"id\":%d,\"result\":{\"stdout\":\"%s\",\"exitcode\":%d}}\n",
             id, result, exitcode);
}

/* read a file from the sandbox */
static void tool_read(mcp_ctx_t *ctx, const char *params, int id,
                      char *out, int out_len) {
    const char *path_str = json_get_string(params, "path");
    if (!path_str) {
        json_error(id, "missing 'path'", out, out_len);
        return;
    }
    char path[1024];
    json_copy_string(path_str, path, sizeof(path));

    /* Resolve path relative to rootfs */
    char full[1024];
    if (path[0] == '/')
        snprintf(full, sizeof(full), "%s%s", ctx->rootfs, path);
    else
        snprintf(full, sizeof(full), "%s%s", ctx->rootfs, path);

    FILE *fp = fopen(full, "rb");
    if (!fp) {
        json_error(id, strerror(errno), out, out_len);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);

    char *content = malloc((size_t)fsize + 1);
    if (!content) {
        fclose(fp);
        json_error(id, "out of memory", out, out_len);
        return;
    }
    size_t nread = fread(content, 1, (size_t)fsize, fp);
    content[nread] = '\0';
    fclose(fp);

    json_result_str(id, "content", content, out, out_len);
    free(content);
}

/* edit: write content to a file (atomic replace) */
static void tool_edit(mcp_ctx_t *ctx, const char *params, int id,
                      char *out, int out_len) {
    const char *path_str = json_get_string(params, "path");
    const char *content_str = json_get_string(params, "content");
    if (!path_str || !content_str) {
        json_error(id, "missing 'path' or 'content'", out, out_len);
        return;
    }
    char path[1024];
    char content[65536];
    json_copy_string(path_str, path, sizeof(path));
    json_copy_string(content_str, content, sizeof(content));

    char full[1024];
    if (path[0] == '/')
        snprintf(full, sizeof(full), "%s%s", ctx->rootfs, path);
    else
        snprintf(full, sizeof(full), "%s%s", ctx->rootfs, path);

    /* Write to temp file first, then rename for atomicity */
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s.tmp", full);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) {
        json_error(id, strerror(errno), out, out_len);
        return;
    }
    fwrite(content, 1, strlen(content), fp);
    fclose(fp);

    if (rename(tmp, full) < 0) {
        json_error(id, strerror(errno), out, out_len);
        unlink(tmp);
        return;
    }

    json_result_int(id, "ok", 1, out, out_len);
}

/* stat: file metadata */
static void tool_stat(mcp_ctx_t *ctx, const char *params, int id,
                      char *out, int out_len) {
    const char *path_str = json_get_string(params, "path");
    if (!path_str) {
        json_error(id, "missing 'path'", out, out_len);
        return;
    }
    char path[1024];
    json_copy_string(path_str, path, sizeof(path));

    char full[1024];
    if (path[0] == '/')
        snprintf(full, sizeof(full), "%s%s", ctx->rootfs, path);
    else
        snprintf(full, sizeof(full), "%s%s", ctx->rootfs, path);

    struct stat st;
    if (stat(full, &st) < 0) {
        json_error(id, strerror(errno), out, out_len);
        return;
    }

    snprintf(out, (size_t)out_len,
             "{\"id\":%d,\"result\":{\"size\":%ld,\"mode\":%o,\"isdir\":%s}}\n",
             id, (long)st.st_size, (unsigned int)st.st_mode,
             S_ISDIR(st.st_mode) ? "true" : "false");
}

/* glob: file pattern search */
static void tool_glob(mcp_ctx_t *ctx, const char *params, int id,
                      char *out, int out_len) {
    const char *pattern_str = json_get_string(params, "pattern");
    if (!pattern_str) {
        json_error(id, "missing 'pattern'", out, out_len);
        return;
    }
    char pattern[1024];
    json_copy_string(pattern_str, pattern, sizeof(pattern));

    /* Search patterns within rootfs */
    char full[1024];
    snprintf(full, sizeof(full), "%s%s", ctx->rootfs, pattern);

    glob_t g;
    int ret = glob(full, 0, NULL, &g);
    if (ret != 0) {
        json_result_str(id, "files", "", out, out_len);
        return;
    }

    /* Build JSON array of matching paths */
    /* Strip rootfs prefix from results */
    char result[65536];
    int pos = 0;
    int rootfs_len = (int)strlen(ctx->rootfs);
    pos += snprintf(result, sizeof(result), "[");
    for (size_t i = 0; i < g.gl_pathc && pos < (int)sizeof(result) - 100; i++) {
        const char *rel_path = g.gl_pathv[i] + rootfs_len;
        if (i > 0) pos += snprintf(result + pos, sizeof(result) - (size_t)pos, ",");
        pos += snprintf(result + pos, sizeof(result) - (size_t)pos, "\"%s\"", rel_path);
    }
    pos += snprintf(result + pos, sizeof(result) - (size_t)pos, "]");
    globfree(&g);

    snprintf(out, (size_t)out_len,
             "{\"id\":%d,\"result\":{\"files\":%s}}\n", id, result);
}

/* grep: search content in a file */
static void tool_grep(mcp_ctx_t *ctx, const char *params, int id,
                      char *out, int out_len) {
    const char *p_str = json_get_string(params, "pattern");
    const char *path_str = json_get_string(params, "path");
    if (!p_str || !path_str) {
        json_error(id, "missing 'pattern' or 'path'", out, out_len);
        return;
    }
    char pattern[1024];
    char filepath[1024];
    json_copy_string(p_str, pattern, sizeof(pattern));
    json_copy_string(path_str, filepath, sizeof(filepath));

    char full[1024];
    if (filepath[0] == '/')
        snprintf(full, sizeof(full), "%s%s", ctx->rootfs, filepath);
    else
        snprintf(full, sizeof(full), "%s%s", ctx->rootfs, filepath);

    FILE *fp = fopen(full, "r");
    if (!fp) {
        json_error(id, strerror(errno), out, out_len);
        return;
    }

    char result[65536];
    int pos = 0;
    pos += snprintf(result, sizeof(result), "[");
    char line[4096];
    int first = 1;
    while (fgets(line, sizeof(line), fp) && pos < (int)sizeof(result) - 500) {
        /* Remove trailing newline */
        int llen = (int)strlen(line);
        if (llen > 0 && line[llen-1] == '\n') line[llen-1] = '\0';

        if (strstr(line, pattern)) {
            if (!first) pos += snprintf(result + pos, sizeof(result) - (size_t)pos, ",");
            first = 0;
            /* JSON-escape the line */
            pos += snprintf(result + pos, sizeof(result) - (size_t)pos, "\"");
            for (char *cp = line; *cp && pos < (int)sizeof(result) - 10; cp++) {
                if (*cp == '\"') pos += snprintf(result + pos, sizeof(result) - (size_t)pos, "\\\"");
                else if (*cp == '\\') pos += snprintf(result + pos, sizeof(result) - (size_t)pos, "\\\\");
                else if (*cp == '\n') pos += snprintf(result + pos, sizeof(result) - (size_t)pos, "\\n");
                else result[pos++] = *cp;
            }
            pos += snprintf(result + pos, sizeof(result) - (size_t)pos, "\"");
        }
    }
    fclose(fp);
    pos += snprintf(result + pos, sizeof(result) - (size_t)pos, "]");

    snprintf(out, (size_t)out_len,
             "{\"id\":%d,\"result\":{\"matches\":%s}}\n", id, result);
}

/* screen: get current PTY screen content */
static void tool_screen(mcp_ctx_t *ctx, const char *params, int id,
                        char *out, int out_len) {
    (void)params;
    const char *content = pty_screen_get(ctx->pty);
    json_result_str(id, "content", content, out, out_len);
}

/* input: inject keyboard/mouse event into PTY */
static void tool_input(mcp_ctx_t *ctx, const char *params, int id,
                       char *out, int out_len) {
    const char *type_str = json_get_string(params, "type");
    const char *key_str = json_get_string(params, "key");

    if (!type_str || !key_str) {
        json_error(id, "missing 'type' or 'key'", out, out_len);
        return;
    }
    char type[64];
    char key[64];
    json_copy_string(type_str, type, sizeof(type));
    json_copy_string(key_str, key, sizeof(key));

    int ret;
    if (strcmp(type, "keydown") == 0 || strcmp(type, "keyup") == 0) {
        ret = pty_inject_key(ctx->pty, type, key);
    } else if (strcmp(type, "mousedown") == 0 || strcmp(type, "mouseup") == 0 ||
               strcmp(type, "mousemove") == 0) {
        /* Extract x, y, btn from params */
        int x = 0, y = 0, btn = 0;
        const char *xs = json_get_string(params, "x");
        const char *ys = json_get_string(params, "y");
        const char *bs = json_get_string(params, "button");
        if (xs) x = atoi(xs);
        if (ys) y = atoi(ys);
        if (bs) btn = atoi(bs);
        ret = pty_inject_mouse(ctx->pty, type, x, y, btn);
    } else {
        json_error(id, "unknown event type", out, out_len);
        return;
    }

    json_result_int(id, "ok", ret >= 0 ? 1 : 0, out, out_len);
}

/* proc: list processes */
static void tool_proc(mcp_ctx_t *ctx, const char *params, int id,
                      char *out, int out_len) {
    (void)ctx;
    (void)params;
    /* We use the PTY child PID if available */
    int pid = ctx->pty->child_pid;
    snprintf(out, (size_t)out_len,
             "{\"id\":%d,\"result\":{\"pid\":%d}}\n", id, pid);
}

/* spawn: start a background process inside the PTY */
static void tool_spawn(mcp_ctx_t *ctx_ __attribute__((unused)), const char *params, int id,
                       char *out, int out_len) {
    const char *cmd_str = json_get_string(params, "cmd");
    if (!cmd_str) {
        json_error(id, "missing 'cmd'", out, out_len);
        return;
    }
    char cmd[8192];
    json_copy_string(cmd_str, cmd, sizeof(cmd));

    /* Fork and exec in background */
    int pid = fork();
    if (pid < 0) {
        json_error(id, strerror(errno), out, out_len);
        return;
    }
    if (pid == 0) {
        /* Child: execute via system() as simple approach */
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    json_result_int(id, "pid", pid, out, out_len);
}

/* ── Request dispatcher ───────────────────────────────────────────────── */

int mcp_handle_request(mcp_ctx_t *ctx, const char *request,
                       char *out, int out_len) {
    /* Extract "method" and "id" */
    const char *method_str = json_get_string(request, "method");
    const char *id_str = json_get_string(request, "id");
    const char *params_str = strstr(request, "\"params\"");

    if (!method_str) {
        snprintf(out, (size_t)out_len, "{\"error\":\"missing method\"}\n");
        return -1;
    }

    int id = id_str ? atoi(id_str) : 0;

    /* Find params value start */
    const char *params_val = NULL;
    if (params_str) {
        params_str += 8; /* skip "params" */
        while (*params_str && (isspace((unsigned char)*params_str) || *params_str == ':'))
            params_str++;
        if (*params_str == '{')
            params_val = params_str;  /* params is an object */
    }
    if (!params_val) params_val = "{}";

    char method[64];
    int mi = 0;
    while (method_str[mi] && method_str[mi] != '\"' && mi < 63) {
        method[mi] = method_str[mi];
        mi++;
    }
    method[mi] = '\0';

    if (strcmp(method, "sh") == 0)
        tool_sh(ctx, params_val, id, out, out_len);
    else if (strcmp(method, "read") == 0)
        tool_read(ctx, params_val, id, out, out_len);
    else if (strcmp(method, "edit") == 0)
        tool_edit(ctx, params_val, id, out, out_len);
    else if (strcmp(method, "stat") == 0)
        tool_stat(ctx, params_val, id, out, out_len);
    else if (strcmp(method, "glob") == 0)
        tool_glob(ctx, params_val, id, out, out_len);
    else if (strcmp(method, "grep") == 0)
        tool_grep(ctx, params_val, id, out, out_len);
    else if (strcmp(method, "screen") == 0)
        tool_screen(ctx, params_val, id, out, out_len);
    else if (strcmp(method, "input") == 0)
        tool_input(ctx, params_val, id, out, out_len);
    else if (strcmp(method, "proc") == 0)
        tool_proc(ctx, params_val, id, out, out_len);
    else if (strcmp(method, "spawn") == 0)
        tool_spawn(ctx, params_val, id, out, out_len);
    else {
        snprintf(out, (size_t)out_len,
                 "{\"id\":%d,\"error\":\"unknown method: %s\"}\n", id, method);
        return -1;
    }

    return 0;
}

/* ── Server loops ─────────────────────────────────────────────────────── */

int mcp_run_stdio(mcp_ctx_t *ctx) {
    char line[65536];
    char response[65536];

    while (fgets(line, sizeof(line), stdin)) {
        /* Skip empty lines */
        size_t len = strlen(line);
        if (len < 2) continue;

        /* Remove trailing newline */
        if (line[len-1] == '\n') line[len-1] = '\0';

        mcp_handle_request(ctx, line, response, sizeof(response));
        fputs(response, stdout);
        fflush(stdout);
    }

    return 0;
}

int mcp_run_unix(mcp_ctx_t *ctx, const char *socket_path) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    /* Remove existing socket file */
    unlink(socket_path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "mcp: socket: %s\n", strerror(errno));
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "mcp: bind: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 1) < 0) {
        fprintf(stderr, "mcp: listen: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    int client = accept(fd, NULL, NULL);
    if (client < 0) {
        fprintf(stderr, "mcp: accept: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);

    /* Duplicate client socket to stdin/stdout */
    dup2(client, 0);
    dup2(client, 1);
    if (client > 1) close(client);

    return mcp_run_stdio(ctx);
}