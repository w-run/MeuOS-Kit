/* file — 文件类型识别
 * 用法：file [-b] [-i] FILE...
 * 选项：-b 不显示文件名前缀, -i MIME 类型
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char version[] = "0.1.0-file (meuos-utils)";

static const char *check_elf(const unsigned char *buf, size_t len) {
    if (len < 16) return NULL;
    if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') return NULL;
    int is64 = (buf[4] == 2);
    int isle = (buf[5] == 1);
    int type;
    if (isle) type = buf[16] | (buf[17] << 8);
    else type = (buf[16] << 8) | buf[17];
    const char *type_str;
    switch (type) {
    case 1: type_str = "relocatable"; break;
    case 2: type_str = "executable"; break;
    case 3: type_str = "shared object"; break;
    case 4: type_str = "core file"; break;
    default: type_str = "unknown"; break;
    }
    static char r[128];
    snprintf(r, sizeof(r), "ELF %d-bit %s %s",
             is64 ? 64 : 32, isle ? "LSB" : "MSB", type_str);
    return r;
}

static const char *check_text(const unsigned char *buf, size_t len) {
    int printable = 0, binary = 0;
    for (size_t i = 0; i < len && i < 1024; i++) {
        if (buf[i] == 0) { binary = 1; break; }
        if (isprint(buf[i]) || isspace(buf[i])) printable++;
        else binary++;
    }
    if (binary > 0) return "data";
    if (len > 0) {
        /* 检查首行是否是 shebang */
        if (len >= 2 && buf[0] == '#' && buf[1] == '!') {
            /* 提取解释器名 */
            static char r[256];
            size_t end = 2;
            while (end < len && end < 255 && buf[end] != '\n' && buf[end] != '\r') end++;
            memcpy(r, buf, end);
            r[end] = '\0';
            return r;
        }
        /* 检查是否是 JSON */
        if (len > 0 && (buf[0] == '{' || buf[0] == '[')) return "JSON text";
    }
    return "ASCII text";
}

static void identify(const char *fname, int brief, int mime) {
    struct stat st;
    if (lstat(fname, &st) < 0) {
        fprintf(stderr, "file: %s: %s\n", fname, strerror(errno));
        return;
    }
    const char *desc = "unknown";

    if (S_ISLNK(st.st_mode)) {
        char target[4096];
        ssize_t n = readlink(fname, target, sizeof(target) - 1);
        if (n > 0) { target[n] = '\0'; desc = "symbolic link"; }
        if (brief) printf("%s\n", desc);
        else printf("%s: %s\n", fname, desc);
        return;
    }
    if (S_ISDIR(st.st_mode)) desc = "directory";
    else if (S_ISCHR(st.st_mode)) desc = "character special";
    else if (S_ISBLK(st.st_mode)) desc = "block special";
    else if (S_ISFIFO(st.st_mode)) desc = "fifo (named pipe)";
    else if (S_ISSOCK(st.st_mode)) desc = "socket";
    else if (S_ISREG(st.st_mode)) {
        FILE *f = fopen(fname, "rb");
        if (!f) { desc = "cannot open"; }
        else {
            unsigned char buf[1024];
            size_t n = fread(buf, 1, sizeof(buf), f);
            fclose(f);
            const char *elf = check_elf(buf, n);
            if (elf) desc = elf;
            else desc = check_text(buf, n);
        }
    }

    if (mime) {
        /* 简化 MIME */
        if (S_ISDIR(st.st_mode)) desc = "inode/directory";
        else if (strstr(desc, "text") || desc[0] == '#') desc = "text/plain";
        else if (strstr(desc, "ELF")) desc = "application/x-executable";
        else desc = "application/octet-stream";
    }
    if (brief) printf("%s\n", desc);
    else printf("%s: %s\n", fname, desc);
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("file %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) { printf("Usage: file [-b] [-i] FILE...\n"); return 0; }
    int brief = 0, mime = 0, argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            if (*p == 'b') brief = 1;
            else if (*p == 'i') mime = 1;
            else { fprintf(stderr, "file: unknown option -%c\n", *p); return 2; }
        }
        argi++;
    }
    if (argi >= argc) { fprintf(stderr, "file: missing operand\n"); return 2; }
    for (int i = argi; i < argc; i++)
        identify(argv[i], brief, mime);
    return 0;
}
