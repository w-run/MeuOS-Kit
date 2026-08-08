#include "mbox/http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>

/* ── Minimal SHA-1 (RFC 3174) ─────────────────────────────────────────── */

typedef struct {
    unsigned H[5];
    unsigned long long count;
    unsigned char buf[64];
} sha1_ctx;

static void sha1_init(sha1_ctx *ctx) {
    ctx->H[0] = 0x67452301;
    ctx->H[1] = 0xEFCDAB89;
    ctx->H[2] = 0x98BADCFE;
    ctx->H[3] = 0x10325476;
    ctx->H[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

#define SHA1_ROTL(x,n) (((x) << (n)) | ((x) >> (32-(n))))

static void sha1_transform(sha1_ctx *ctx, const unsigned char block[64]) {
    unsigned W[80];
    for (int i = 0; i < 16; i++)
        W[i] = ((unsigned)block[i*4] << 24) | (block[i*4+1] << 16) |
               (block[i*4+2] << 8) | block[i*4+3];
    for (int i = 16; i < 80; i++)
        W[i] = SHA1_ROTL(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1);

    unsigned a = ctx->H[0], b = ctx->H[1], c = ctx->H[2];
    unsigned d = ctx->H[3], e = ctx->H[4], f, k;

    for (int i = 0; i < 80; i++) {
        if (i < 20)      { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;          k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;          k = 0xCA62C1D6; }
        unsigned tmp = SHA1_ROTL(a, 5) + f + e + k + W[i];
        e = d; d = c; c = SHA1_ROTL(b, 30); b = a; a = tmp;
    }

    ctx->H[0] += a; ctx->H[1] += b; ctx->H[2] += c;
    ctx->H[3] += d; ctx->H[4] += e;
}

static void sha1_update(sha1_ctx *ctx, const unsigned char *data, int len) {
    int offset = (int)(ctx->count & 63);
    ctx->count += len;
    int rem = 64 - offset;
    if (len >= rem) {
        memcpy(ctx->buf + offset, data, rem);
        sha1_transform(ctx, ctx->buf);
        for (int i = rem; i + 63 < len; i += 64)
            sha1_transform(ctx, data + i);
        offset = 0;
    }
    memcpy(ctx->buf + offset, data + (len - (len - offset)), (size_t)(len - offset));
}

static void sha1_final(sha1_ctx *ctx, unsigned char digest[20]) {
    unsigned long long bits = ctx->count << 3;
    unsigned char pad[128];
    int padlen = (int)(ctx->count & 63);
    pad[0] = 0x80;
    padlen = (padlen < 56) ? 56 - padlen : 120 - padlen;
    memset(pad + 1, 0, (size_t)(padlen + 7));
    for (int i = 0; i < 8; i++)
        pad[padlen + 1 + i] = (unsigned char)(bits >> (56 - i * 8));
    sha1_update(ctx, pad, padlen + 1 + 8);
    for (int i = 0; i < 5; i++)
        digest[i*4] = (unsigned char)(ctx->H[i] >> 24),
        digest[i*4+1] = (unsigned char)(ctx->H[i] >> 16),
        digest[i*4+2] = (unsigned char)(ctx->H[i] >> 8),
        digest[i*4+3] = (unsigned char)ctx->H[i];
}

/* ── Base64 ───────────────────────────────────────────────────────────── */

static const char b64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const unsigned char *in, int in_len,
                          char *out, int out_max) {
    int i = 0, o = 0;
    while (i < in_len && o < out_max - 4) {
        unsigned a = in[i++];
        unsigned b = i < in_len ? in[i++] : 0;
        unsigned c = i < in_len ? in[i++] : 0;
        out[o++] = b64_chars[a >> 2];
        out[o++] = b64_chars[((a & 3) << 4) | (b >> 4)];
        out[o++] = (i - 1 < in_len) ? b64_chars[((b & 0xF) << 2) | (c >> 6)] : '=';
        out[o++] = (i < in_len) ? b64_chars[c & 0x3F] : '=';
    }
    out[o] = '\0';
}

/* ── HTTP server core ─────────────────────────────────────────────────── */

static const char *html_page =
    "<!DOCTYPE html>\n"
    "<html><head><meta charset=\"utf-8\"><title>mbox WebPTY</title>\n"
    "<style>\n"
    "  body{background:#1a1a1a;color:#f0f0f0;margin:0;padding:8px;font-family:monospace;font-size:14px}\n"
    "  #term{white-space:pre-wrap;word-wrap:break-word;overflow-y:auto;height:95vh;background:#111;padding:8px;border:1px solid #333}\n"
    "  #input{display:none}\n"
    "  .ansi-fg-30{color:#555}.ansi-fg-31{color:#e55}.ansi-fg-32{color:#5e5}\n"
    "  .ansi-fg-33{color:#ee5}.ansi-fg-34{color:#55e}.ansi-fg-35{color:#e5e}\n"
    "  .ansi-fg-36{color:#5ee}.ansi-fg-37{color:#ccc}.ansi-fg-90{color:#888}\n"
    "  .ansi-fg-91{color:#f88}.ansi-fg-92{color:#8f8}.ansi-fg-93{color:#ff8}\n"
    "  .ansi-fg-94{color:#88f}.ansi-fg-95{color:#f8f}.ansi-fg-96{color:#8ff}\n"
    "  .ansi-fg-97{color:#fff}.ansi-bold{font-weight:bold}\n"
    "</style>\n"
    "</head><body>\n"
    "<div id=\"term\"></div>\n"
    "<script>\n"
    "var term=document.getElementById('term');\n"
    "var ws=new WebSocket('ws://'+location.host+'/pty');\n"
    "var pending='';\n"
    "var ansi_re=/\\033\\[([0-9;]*)m/g;\n"
    "function escapeHTML(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}\n"
    "function render(text){\n"
    "  pending+=text;\n"
    "  var parts=pending.split('\\n');\n"
    "  if(parts.length>1){\n"
    "    var lastPending=parts.pop();\n"
    "    var html='';\n"
    "    for(var i=0;i<parts.length;i++){\n"
    "      var line=parts[i];\n"
    "      var bold=false,fgs='';\n"
    "      line=line.replace(ansi_re,function(m,codes){\n"
    "        var cs=codes.split(';');\n"
    "        for(var j=0;j<cs.length;j++){\n"
    "          var c=parseInt(cs[j],10);\n"
    "          if(c===0){bold=false;fgs=''}\n"
    "          else if(c===1)bold=true;\n"
    "          else if(c>=30&&c<=37)fgs='ansi-fg-'+(30+(c-30));\n"
    "          else if(c>=90&&c<=97)fgs='ansi-fg-'+(90+(c-90));\n"
    "        }\n"
    "        return '';\n"
    "      });\n"
    "      var cls=fgs+(bold?' ansi-bold':'');\n"
    "      html+='<div'+(cls?' class=\"'+cls+'\"':'')+'>'+escapeHTML(line)+'</div>\\n';\n"
    "    }\n"
    "    term.innerHTML+=html;\n"
    "    pending=lastPending;\n"
    "    term.scrollTop=term.scrollHeight;\n"
    "  }\n"
    "}\n"
    "ws.onmessage=function(e){render(e.data)};\n"
    "ws.onopen=function(){\n"
    "  document.onkeydown=function(e){\n"
    "    var k=e.key;if(k==='Shift'||k==='Control'||k==='Alt')return;\n"
    "    ws.send(JSON.stringify({type:'keydown',key:k}));\n"
    "    if(k==='Enter')render('\\n');\n"
    "    e.preventDefault();\n"
    "  };\n"
    "  document.onkeyup=function(e){\n"
    "    var k=e.key;if(k==='Shift'||k==='Control'||k==='Alt')return;\n"
    "    ws.send(JSON.stringify({type:'keyup',key:k}));\n"
    "  };\n"
    "  term.onclick=function(){term.focus();};\n"
    "  term.setAttribute('tabindex','0');\n"
    "};\n"
    "</script></body></html>\n";

void http_init(http_server_t *srv) {
    memset(srv, 0, sizeof(*srv));
    srv->listen_fd = -1;
    srv->client_fd = -1;
}

int http_listen(http_server_t *srv, int port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { perror("http: socket"); return -1; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "http: bind %d: %s\n", port, strerror(errno));
        close(fd); return -1;
    }
    if (listen(fd, 5) < 0) {
        fprintf(stderr, "http: listen: %s\n", strerror(errno));
        close(fd); return -1;
    }
    srv->listen_fd = fd;
    srv->port = port;
    return 0;
}

int http_accept(http_server_t *srv) {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int fd = accept(srv->listen_fd, (struct sockaddr *)&addr, &addrlen);
    if (fd < 0) { perror("http: accept"); return -1; }
    srv->client_fd = fd;
    srv->is_websocket = 0;
    return fd;
}

static int read_line(int fd, char *buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c;
        int n = (int)read(fd, &c, 1);
        if (n <= 0) break;
        if (c == '\r') continue;
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

int http_read_request(http_server_t *srv, char *buf__unused, int buf_len__unused) {
    (void)buf__unused;
    (void)buf_len__unused;
    int fd = srv->client_fd;
    if (fd < 0) return -1;

    char line[4096];
    if (read_line(fd, line, sizeof(line)) <= 0) return -1;

    char method[32], path[1024];
    if (sscanf(line, "%31s %1023s", method, path) < 2) return -1;
    int is_get = (strcmp(method, "GET") == 0);

    char ws_key[256] = "";
    int found_upgrade = 0;

    while (1) {
        int n = read_line(fd, line, sizeof(line));
        if (n <= 0) break;
        if (strncasecmp(line, "Upgrade:", 8) == 0 &&
            strstr(line, "websocket"))
            found_upgrade = 1;
        if (strncasecmp(line, "Sec-WebSocket-Key:", 18) == 0) {
            const char *v = line + 18;
            while (*v && isspace((unsigned char)*v)) v++;
            snprintf(ws_key, sizeof(ws_key), "%.255s", v);
        }
    }

    /* WebSocket upgrade */
    if (found_upgrade && ws_key[0]) {
        if (ws_handshake(srv, ws_key) == 0) {
            srv->is_websocket = 1;
            return 0;
        }
    }

    /* Regular GET */
    if (is_get) {
        http_send_response(srv, 200, "text/html; charset=utf-8",
                           html_page, (int)strlen(html_page));
    } else {
        http_send_response(srv, 405, "text/plain", "Method Not Allowed", 18);
    }
    return 0;
}

int http_send_response(http_server_t *srv, int status,
                       const char *content_type,
                       const char *body, int body_len) {
    int fd = srv->client_fd;
    if (fd < 0) return -1;

    char header[1024];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status,
        status == 200 ? "OK" :
        status == 404 ? "Not Found" : "Unknown",
        content_type, body_len);

    write(fd, header, (size_t)n);
    if (body && body_len > 0) write(fd, body, (size_t)body_len);
    return n + body_len;
}

/* ── WebSocket (RFC 6455) ─────────────────────────────────────────────── */

int ws_handshake(http_server_t *srv, const char *key) {
    int fd = srv->client_fd;
    if (fd < 0) return -1;

    const char *magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concat[512];
    snprintf(concat, sizeof(concat), "%s%s", key, magic);

    unsigned char sha1_out[20];
    sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, (unsigned char *)concat, (int)strlen(concat));
    sha1_final(&ctx, sha1_out);

    char accept_b64[64];
    base64_encode(sha1_out, 20, accept_b64, (int)sizeof(accept_b64));

    char response[1024];
    int n = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept_b64);

    write(fd, response, (size_t)n);
    return 0;
}

int ws_send(http_server_t *srv, int opcode, const char *data, int len) {
    int fd = srv->client_fd;
    if (fd < 0) return -1;

    unsigned char header[10];
    int hlen;

    if (len < 126) {
        header[0] = (unsigned char)(0x80 | opcode);
        header[1] = (unsigned char)len;
        hlen = 2;
    } else if (len < 65536) {
        header[0] = (unsigned char)(0x80 | opcode);
        header[1] = 126;
        header[2] = (unsigned char)((len >> 8) & 0xFF);
        header[3] = (unsigned char)(len & 0xFF);
        hlen = 4;
    } else {
        header[0] = (unsigned char)(0x80 | opcode);
        header[1] = 127;
        for (int i = 0; i < 8; i++)
            header[2 + i] = (unsigned char)((len >> (56 - 8 * i)) & 0xFF);
        hlen = 10;
    }

    write(fd, header, (size_t)hlen);
    write(fd, data, (size_t)len);
    return hlen + len;
}

int ws_recv(http_server_t *srv, char *buf, int buf_len) {
    int fd = srv->client_fd;
    if (fd < 0) return -1;

    unsigned char hdr[2];
    int n = (int)read(fd, hdr, 2);
    if (n < 2) return n == 0 ? 0 : -1;

    int opcode = hdr[0] & 0x0F;
    int masked = (hdr[1] >> 7) & 1;
    int paylen = hdr[1] & 0x7F;

    if (paylen == 126) {
        unsigned char ext[2];
        if (read(fd, ext, 2) < 2) return -1;
        paylen = (ext[0] << 8) | ext[1];
    } else if (paylen == 127) {
        unsigned char ext[8];
        if (read(fd, ext, 8) < 8) return -1;
        paylen = 0;
        for (int i = 0; i < 4; i++) paylen = (paylen << 8) | ext[i];
    }

    unsigned char mask[4];
    if (masked && read(fd, mask, 4) < 4) return -1;

    if (paylen > buf_len - 1) paylen = buf_len - 1;
    n = (int)read(fd, buf, (size_t)paylen);
    if (n <= 0) return n;

    if (masked)
        for (int i = 0; i < n; i++) buf[i] = (char)(buf[i] ^ mask[i & 3]);
    buf[n] = '\0';

    if (opcode == WS_OPCODE_CLOSE) return 0;
    if (opcode == WS_OPCODE_PING) { ws_send(srv, WS_OPCODE_PONG, buf, n); return 0; }
    if (opcode == WS_OPCODE_PONG) return 0;
    return n;
}

void http_close(http_server_t *srv) {
    if (srv->client_fd >= 0) { close(srv->client_fd); srv->client_fd = -1; }
    if (srv->listen_fd >= 0) { close(srv->listen_fd); srv->listen_fd = -1; }
    srv->is_websocket = 0;
}