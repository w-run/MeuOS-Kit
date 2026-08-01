/* tar — POSIX pax 格式归档工具
 * 支持：创建/解包/列表，pax 扩展头，gzip 透传（-z）
 * 用法：tar {-c|-x|-t} [options] [files...]
 *   -c   创建归档
 *   -x   解包归档
 *   -t   列出内容
 *   -f FILE  指定归档文件（- 表示 stdin/stdout）
 *   -z   gzip 压缩/解压
 *   -v   verbose
 *   -C DIR  切换到 DIR 目录
 *   --help / --version
 */

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <utime.h>

#define BLOCK 512
#define NAME_MAX_TAR 100

static const char *prog = "tar";

static void die(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "%s: ", prog); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap); exit(2);
}

/* === pax 头部 === */
typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12]; /* 凑齐 512 */
} tar_header_t;

static void init_header(tar_header_t *h) {
    memset(h, 0, sizeof(*h));
    memcpy(h->magic, "ustar", 5);
    h->magic[5] = ' ';
    h->version[0] = '0'; h->version[1] = '0';
    h->typeflag = '0'; /* regular file */
}

static unsigned calc_chksum(const tar_header_t *h) {
    const unsigned char *p = (const unsigned char *)h;
    unsigned sum = 0;
    for (int i = 0; i < BLOCK; i++) {
        if (i >= 148 && i < 156) sum += ' '; /* chksum field treated as spaces */
        else sum += p[i];
    }
    return sum;
}

static int write_octal(char *buf, int len, unsigned long val) {
    memset(buf, '0', len - 1);
    buf[len - 1] = ' ';
    int pos = len - 2;
    while (val > 0 && pos >= 0) {
        buf[pos--] = '0' + (val % 8);
        val /= 8;
    }
    return 0;
}

static unsigned long parse_octal(const char *buf, int len) {
    unsigned long val = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] >= '0' && buf[i] <= '7') val = val * 8 + (buf[i] - '0');
        else if (buf[i] == ' ' || buf[i] == '\0') continue;
        else break;
    }
    return val;
}

static int write_header(int fd, const char *name, const struct stat *st,
                        char typeflag, const char *linkname) {
    tar_header_t h;
    init_header(&h);
    
    /* Handle long names with pax extended header */
    if (strlen(name) > NAME_MAX_TAR) {
        /* Write pax extended header record */
        tar_header_t ph;
        init_header(&ph);
        ph.typeflag = 'L'; /* GNU long name */
        char *paxrec = NULL;
        int pathlen = strlen(name);
        int reclen = snprintf(NULL, 0, "%d path=%s\n", pathlen, name);
        paxrec = malloc(reclen + 1);
        if (!paxrec) die("malloc failed");
        snprintf(paxrec, reclen + 1, "%d path=%s\n", pathlen, name);
        snprintf(paxrec, reclen + 1, "%d path=%s\n", pathlen, name);
        
        write_octal(ph.size, sizeof(ph.size), strlen(paxrec));
        unsigned cs = calc_chksum(&ph);
        write_octal(ph.chksum, sizeof(ph.chksum) - 1, cs);
        ph.chksum[6] = '\0'; ph.chksum[7] = ' ';
        write(fd, &ph, BLOCK);
        write(fd, paxrec, strlen(paxrec));
        int pad = BLOCK - (strlen(paxrec) % BLOCK);
        if (pad < BLOCK) {
            char z[BLOCK] = {0};
            write(fd, z, pad);
        }
        free(paxrec);
        strncpy(h.name, name, NAME_MAX_TAR); /* truncated, pax overrides */
    } else {
        strncpy(h.name, name, NAME_MAX_TAR - 1);
    }
    
    write_octal(h.mode, sizeof(h.mode) - 1, st->st_mode & 07777);
    write_octal(h.uid, sizeof(h.uid) - 1, st->st_uid);
    write_octal(h.gid, sizeof(h.gid) - 1, st->st_gid);
    write_octal(h.size, sizeof(h.size) - 1, typeflag == '0' ? (unsigned long)st->st_size : 0);
    write_octal(h.mtime, sizeof(h.mtime) - 1, st->st_mtime);
    h.typeflag = typeflag;
    if (linkname) strncpy(h.linkname, linkname, NAME_MAX_TAR - 1);
    
    unsigned cs = calc_chksum(&h);
    write_octal(h.chksum, sizeof(h.chksum) - 1, cs);
    h.chksum[6] = '\0'; h.chksum[7] = ' ';
    
    write(fd, &h, BLOCK);
    return 0;
}

static int write_padding(int fd, size_t size) {
    int pad = BLOCK - (size % BLOCK);
    if (pad == BLOCK) return 0;
    char z[BLOCK] = {0};
    return write(fd, z, pad);
}

/* === 创建归档 === */
static void archive_file(int fd, const char *path, const char *arcname, int verbose) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
        return;
    }
    
    char typeflag;
    char linkname[PATH_MAX] = {0};
    
    if (S_ISDIR(st.st_mode)) typeflag = '5';
    else if (S_ISLNK(st.st_mode)) {
        typeflag = '2';
        ssize_t n = readlink(path, linkname, sizeof(linkname) - 1);
        if (n < 0) linkname[0] = '\0';
    }
    else typeflag = '0';
    
    if (verbose) fprintf(stderr, "%s\n", arcname);
    write_header(fd, arcname, &st, typeflag, linkname[0] ? linkname : NULL);
    
    if (typeflag == '0' && S_ISREG(st.st_mode)) {
        int in = open(path, O_RDONLY);
        if (in < 0) {
            fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
            return;
        }
        char buf[8192];
        ssize_t n;
        size_t total = 0;
        while ((n = read(in, buf, sizeof(buf))) > 0) {
            write(fd, buf, n);
            total += n;
        }
        close(in);
        write_padding(fd, total);
    }
}

static void archive_dir(int fd, const char *path, const char *prefix, int verbose) {
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char fpath[PATH_MAX];
        snprintf(fpath, sizeof(fpath), "%s/%s", path, e->d_name);
        char arcname[PATH_MAX];
        if (prefix[0])
            snprintf(arcname, sizeof(arcname), "%s/%s", prefix, e->d_name);
        else
            snprintf(arcname, sizeof(arcname), "%s", e->d_name);
        
        struct stat st;
        if (lstat(fpath, &st) != 0) continue;
        
        archive_file(fd, fpath, arcname, verbose);
        
        if (S_ISDIR(st.st_mode)) {
            char newprefix[PATH_MAX];
            snprintf(newprefix, sizeof(newprefix), "%s", arcname);
            archive_dir(fd, fpath, newprefix, verbose);
        }
    }
    closedir(d);
}

static int do_create(const char *outfile, char **files, int nfiles,
                     int use_gzip, int verbose, const char *chdir_dir) {
    if (chdir_dir && chdir(chdir_dir) != 0)
        die("cannot chdir to %s: %s", chdir_dir, strerror(errno));
    
    int fd;
    int pipefd[2] = {-1, -1};
    
    if (use_gzip) {
        if (pipe(pipefd) < 0) die("pipe failed");
        pid_t pid = fork();
        if (pid < 0) die("fork failed");
        if (pid == 0) {
            /* child: gzip */
            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            if (strcmp(outfile, "-") == 0)
                dup2(STDOUT_FILENO, STDOUT_FILENO);
            else {
                int out = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out < 0) die("cannot create %s: %s", outfile, strerror(errno));
                dup2(out, STDOUT_FILENO);
                close(out);
            }
            execlp("gzip", "gzip", "-c", NULL);
            _exit(127);
        }
        close(pipefd[0]);
        fd = pipefd[1];
    } else {
        if (strcmp(outfile, "-") == 0)
            fd = STDOUT_FILENO;
        else {
            fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) die("cannot create %s: %s", outfile, strerror(errno));
        }
    }
    
    for (int i = 0; i < nfiles; i++) {
        struct stat st;
        if (lstat(files[i], &st) != 0) {
            fprintf(stderr, "%s: %s: %s\n", prog, files[i], strerror(errno));
            continue;
        }
        /* Use basename for archive name */
        char *arcname = files[i];
        /* strip leading ./ */
        while (arcname[0] == '.' && arcname[1] == '/') arcname += 2;
        
        archive_file(fd, files[i], arcname, verbose);
        
        if (S_ISDIR(st.st_mode)) {
            archive_dir(fd, files[i], arcname, verbose);
        }
    }
    
    /* Write two empty blocks to signal end */
    char end[1024] = {0};
    write(fd, end, 1024);
    
    if (fd != STDOUT_FILENO) close(fd);
    if (use_gzip && pipefd[1] >= 0) close(pipefd[1]);
    
    return 0;
}

/* === 解包归档 === */
static int mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    int len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
    return 0;
}

static char *pax_path = NULL; /* override from pax extended header */

static int do_extract(int fd, int verbose, const char *chdir_dir) {
    if (chdir_dir && chdir(chdir_dir) != 0)
        die("cannot chdir to %s: %s", chdir_dir, strerror(errno));
    
    char buf[BLOCK];
    long pending_size = 0;
    
    while (1) {
        ssize_t n = read(fd, buf, BLOCK);
        if (n <= 0) break;
        if (n < BLOCK) break;
        
        /* Check for end of archive (all zeros) */
        int allzero = 1;
        for (int i = 0; i < BLOCK; i++) {
            if (buf[i] != 0) { allzero = 0; break; }
        }
        if (allzero) break;
        
        tar_header_t *h = (tar_header_t *)buf;
        
        /* Verify magic */
        if (memcmp(h->magic, "ustar", 5) != 0) {
            fprintf(stderr, "%s: not a valid tar header\n", prog);
            break;
        }
        
        char name[PATH_MAX];
        char fullname[PATH_MAX];
        
        /* Handle GNU long name (typeflag 'L') */
        if (h->typeflag == 'L') {
            pending_size = parse_octal(h->size, sizeof(h->size));
            char *longname = malloc(pending_size + 1);
            long total_read = 0;
            while (total_read < pending_size) {
                int to_read = (pending_size - total_read < BLOCK) ? 
                    (pending_size - total_read) : BLOCK;
                /* read may need multiple blocks */
                int rd = 0;
                while (rd < ((to_read + BLOCK - 1) / BLOCK) * BLOCK) {
                    ssize_t r = read(fd, longname + total_read, 
                        (to_read - rd < BLOCK) ? (to_read - rd) : BLOCK);
                    if (r <= 0) break;
                    total_read += r;
                    rd += r;
                }
            }
            longname[pending_size] = '\0';
            free(pax_path);
            pax_path = strdup(longname);
            free(longname);
            /* Read padding */
            int pad = BLOCK - (pending_size % BLOCK);
            if (pad < BLOCK) {
                char t[BLOCK];
                read(fd, t, pad);
            }
            /* Next header has the real file data */
            read(fd, buf, BLOCK);
            h = (tar_header_t *)buf;
        }
        
        if (pax_path) {
            strncpy(name, pax_path, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            free(pax_path);
            pax_path = NULL;
        } else {
            /* Combine prefix + name */
            if (h->prefix[0]) {
                snprintf(fullname, sizeof(fullname), "%s/%s", h->prefix, h->name);
            } else {
                strncpy(fullname, h->name, sizeof(fullname) - 1);
                fullname[sizeof(fullname) - 1] = '\0';
            }
            strncpy(name, fullname, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        }
        
        unsigned long size = parse_octal(h->size, sizeof(h->size));
        mode_t mode = parse_octal(h->mode, sizeof(h->mode));
        
        if (verbose) fprintf(stderr, "%s\n", name);
        
        if (h->typeflag == '5') {
            /* Directory */
            mkdir_p(name, mode);
        } else if (h->typeflag == '2') {
            /* Symlink */
            unlink(name);
            char *slash = strrchr(name, '/');
            if (slash) {
                *slash = '\0';
                mkdir_p(name, 0755);
                *slash = '/';
            }
            symlink(h->linkname, name);
        } else {
            /* Regular file */
            char *slash = strrchr(name, '/');
            if (slash) {
                *slash = '\0';
                mkdir_p(name, 0755);
                *slash = '/';
            }
            int out = open(name, O_WRONLY | O_CREAT | O_TRUNC, mode ? mode : 0644);
            if (out < 0) {
                fprintf(stderr, "%s: %s: %s\n", prog, name, strerror(errno));
                /* Skip file data */
                unsigned long remaining = size;
                while (remaining > 0) {
                    int rd = (remaining < BLOCK) ? remaining : BLOCK;
                    char t[BLOCK];
                    read(fd, t, rd);
                    remaining -= rd;
                }
                int pad = BLOCK - (size % BLOCK);
                if (pad < BLOCK) { char t[BLOCK]; read(fd, t, pad); }
                continue;
            }
            
            unsigned long remaining = size;
            char fbuf[8192];
            while (remaining > 0) {
                int rd = (remaining < sizeof(fbuf)) ? remaining : sizeof(fbuf);
                ssize_t r = read(fd, fbuf, rd);
                if (r <= 0) break;
                write(out, fbuf, r);
                remaining -= r;
            }
            close(out);
            
            /* Skip padding */
            int pad = BLOCK - (size % BLOCK);
            if (pad < BLOCK) { char t[BLOCK]; read(fd, t, pad); }
            
            /* Restore mtime */
            time_t mtime = parse_octal(h->mtime, sizeof(h->mtime));
            struct utimbuf ut = { .actime = mtime, .modtime = mtime };
            utime(name, &ut);
        }
    }
    
    return 0;
}

/* === 列表归档 === */
static int do_list(int fd, int verbose) {
    char buf[BLOCK];
    
    while (1) {
        ssize_t n = read(fd, buf, BLOCK);
        if (n <= 0 || n < BLOCK) break;
        
        int allzero = 1;
        for (int i = 0; i < BLOCK; i++) {
            if (buf[i] != 0) { allzero = 0; break; }
        }
        if (allzero) break;
        
        tar_header_t *h = (tar_header_t *)buf;
        if (memcmp(h->magic, "ustar", 5) != 0) break;
        
        char name[PATH_MAX];
        if (h->prefix[0])
            snprintf(name, sizeof(name), "%s/%s", h->prefix, h->name);
        else
            strncpy(name, h->name, sizeof(name) - 1);
        
        unsigned long size = parse_octal(h->size, sizeof(h->size));
        
        /* Skip GNU long name records */
        if (h->typeflag == 'L') {
            unsigned long lsize = size;
            int blocks = (lsize + BLOCK - 1) / BLOCK;
            for (int i = 0; i < blocks; i++) read(fd, buf, BLOCK);
            /* Read actual header */
            n = read(fd, buf, BLOCK);
            if (n <= 0 || n < BLOCK) break;
            h = (tar_header_t *)buf;
            if (h->prefix[0])
                snprintf(name, sizeof(name), "%s/%s", h->prefix, h->name);
            else
                strncpy(name, h->name, sizeof(name) - 1);
            size = parse_octal(h->size, sizeof(h->size));
        }
        
        if (verbose) {
            char perm[11];
            perm[0] = (h->typeflag == '5') ? 'd' : 
                      (h->typeflag == '2') ? 'l' : '-';
            mode_t mode = parse_octal(h->mode, sizeof(h->mode));
            for (int i = 0; i < 9; i++) perm[i+1] = '-';
            int pos = 1;
            if (mode & 0444) { perm[pos]='r'; perm[pos+1]='w'; perm[pos+2]='-'; }
            if (mode & 0400) perm[1]='r';
            if (mode & 0200) perm[2]='w';
            if (mode & 0100) perm[3]='x';
            if (mode & 0040) perm[4]='r';
            if (mode & 0020) perm[5]='w';
            if (mode & 0010) perm[6]='x';
            if (mode & 0004) perm[7]='r';
            if (mode & 0002) perm[8]='w';
            if (mode & 0001) perm[9]='x';
            perm[10] = '\0';
            unsigned uid = parse_octal(h->uid, sizeof(h->uid));
            unsigned gid = parse_octal(h->gid, sizeof(h->gid));
            printf("%s %u/%u %lu %s\n", perm, uid, gid, size, name);
        } else {
            printf("%s\n", name);
        }
        
        /* Skip file data */
        if (h->typeflag == '0' || h->typeflag == '\0') {
            int blocks = (size + BLOCK - 1) / BLOCK;
            for (int i = 0; i < blocks; i++) {
                n = read(fd, buf, BLOCK);
                if (n <= 0) break;
            }
        }
    }
    
    return 0;
}

static void usage(void) {
    fprintf(stderr,
        "usage: tar {-c|-x|-t} [-f ARCHIVE] [-z] [-v] [-C DIR] [files...]\n"
        "  -c  create archive\n"
        "  -x  extract archive\n"
        "  -t  list contents\n"
        "  -f FILE  archive file (- for stdin/stdout)\n"
        "  -z  gzip compress/decompress\n"
        "  -v  verbose\n"
        "  -C DIR  change to DIR\n"
        "  --help     show help\n"
        "  --version  show version\n");
}

int main(int argc, char **argv) {
    int mode = 0; /* 'c', 'x', 't' */
    const char *archive = NULL;
    int use_gzip = 0;
    int verbose = 0;
    const char *chdir_dir = NULL;
    int oi = 1;
    
    /* Parse options */
    while (oi < argc && argv[oi][0] == '-' && argv[oi][1] != '\0') {
        const char *opt = argv[oi];
        if (strcmp(opt, "--help") == 0) { usage(); return 0; }
        if (strcmp(opt, "--version") == 0) {
            printf("tar (meuos-utils)\n");
            return 0;
        }
        
        /* Handle combined short options */
        int j = 1;
        while (opt[j]) {
            switch (opt[j]) {
                case 'c': mode = 'c'; break;
                case 'x': mode = 'x'; break;
                case 't': mode = 't'; break;
                case 'z': use_gzip = 1; break;
                case 'v': verbose = 1; break;
                case 'f':
                    if (opt[j+1]) { archive = &opt[j+1]; j = strlen(opt) - 1; }
                    else if (oi + 1 < argc) { archive = argv[++oi]; }
                    else die("option -f requires an argument");
                    break;
                case 'C':
                    if (opt[j+1]) { chdir_dir = &opt[j+1]; j = strlen(opt) - 1; }
                    else if (oi + 1 < argc) { chdir_dir = argv[++oi]; }
                    else die("option -C requires an argument");
                    break;
                default:
                    die("unknown option -%c", opt[j]);
            }
            j++;
        }
        oi++;
    }
    
    if (!mode) { usage(); return 2; }
    if (!archive) archive = "-"; /* default to stdout/stdin */
    
    if (mode == 'c') {
        char **files = argv + oi;
        int nfiles = argc - oi;
        if (nfiles == 0) die("no files specified for -c");
        return do_create(archive, files, nfiles, use_gzip, verbose, chdir_dir);
    }
    
    /* For -x and -t, open the archive */
    int fd;
    int pipefd[2] = {-1, -1};
    
    if (use_gzip) {
        /* Need to decompress via gzip */
        if (strcmp(archive, "-") == 0) {
            if (pipe(pipefd) < 0) die("pipe failed");
            pid_t pid = fork();
            if (pid < 0) die("fork failed");
            if (pid == 0) {
                /* child: gunzip from stdin to pipe */
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
                execlp("gzip", "gzip", "-dc", NULL);
                _exit(127);
            }
            close(pipefd[1]);
            fd = pipefd[0];
        } else {
            if (pipe(pipefd) < 0) die("pipe failed");
            pid_t pid = fork();
            if (pid < 0) die("fork failed");
            if (pid == 0) {
                close(pipefd[0]);
                int in = open(archive, O_RDONLY);
                if (in < 0) die("cannot open %s: %s", archive, strerror(errno));
                dup2(in, STDIN_FILENO);
                close(in);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
                execlp("gzip", "gzip", "-dc", NULL);
                _exit(127);
            }
            close(pipefd[1]);
            fd = pipefd[0];
        }
    } else {
        if (strcmp(archive, "-") == 0)
            fd = STDIN_FILENO;
        else {
            fd = open(archive, O_RDONLY);
            if (fd < 0) die("cannot open %s: %s", archive, strerror(errno));
        }
    }
    
    int ret;
    if (mode == 'x') ret = do_extract(fd, verbose, chdir_dir);
    else ret = do_list(fd, verbose);
    
    if (fd != STDIN_FILENO) close(fd);
    return ret;
}
