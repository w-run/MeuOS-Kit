/* patch — 应用 diff 补丁
 * 支持 unified diff 格式（`@@ -old,count +new,count @@`）
 *
 * 用法：patch [options] [patchfile]
 *   -p N    去除 N 层路径前缀
 *   -i FILE  从 FILE 读取补丁
 *   -o FILE  输出到 FILE（不修改原文件）
 *   -R      反向应用补丁
 *   -E      移除空文件
 *   --dry-run  只测试不修改
 *   --help / --version
 */

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "meuos/utils.h"

/* program_name provided by libutils.a */

typedef struct {
    char *old_name;
    char *new_name;
    int old_start;
    int old_count;
    int new_start;
    int new_count;
    char **lines;     /* hunks lines (with +/-/ context prefix) */
    int line_count;
} hunk_t;

typedef struct {
    char *filename;
    hunk_t **hunks;
    int hunk_count;
    int hunk_cap;
} file_patch_t;

/* xmalloc/xstrdup provided by libutils.a */

/* Strip N path components from filename */
static char *strip_prefix(const char *path, int strip) {
    if (strip <= 0) return xstrdup(path);
    
    const char *p = path;
    for (int i = 0; i < strip && *p; i++) {
        const char *slash = strchr(p, '/');
        if (!slash) return xstrdup(p);
        p = slash + 1;
    }
    return xstrdup(p);
}

/* Read file lines into array */
static char **read_file_lines(const char *path, int *count) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    
    int cap = 256;
    char **lines = xmalloc(sizeof(char*) * cap);
    *count = 0;
    
    char *line = NULL;
    size_t lcap = 0;
    ssize_t n;
    while ((n = getline(&line, &lcap, f)) > 0) {
        if (*count >= cap) {
            cap *= 2;
            lines = realloc(lines, sizeof(char*) * cap);
        }
        lines[*count] = xstrdup(line);
        (*count)++;
    }
    free(line);
    fclose(f);
    return lines;
}

static void free_lines(char **lines, int count) {
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

/* Write lines to file */
static int write_file_lines(const char *path, char **lines, int count) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < count; i++)
        fputs(lines[i], f);
    fclose(f);
    return 0;
}

/* Parse a unified diff patchfile */
static int parse_patch(const char *patchdata, size_t patchlen, file_patch_t ***patches_out, int *patch_count) {
    file_patch_t **patches = NULL;
    int num_patches = 0;
    int patch_cap = 0;
    
    /* Split into lines */
    char *copy = xmalloc(patchlen + 1);
    memcpy(copy, patchdata, patchlen);
    copy[patchlen] = '\0';
    
    char *saveptr;
    char *line = strtok_r(copy, "\n", &saveptr);
    char **all_lines = xmalloc(sizeof(char*) * 65536);
    int num_lines = 0;
    
    while (line) {
        all_lines[num_lines] = xstrdup(line);
        num_lines++;
        line = strtok_r(NULL, "\n", &saveptr);
    }
    
    int i = 0;
    while (i < num_lines) {
        /* Look for --- filename or diff --git or +++ filename */
        char *old_name = NULL, *new_name = NULL;
        
        /* Skip until we find a file header */
        while (i < num_lines) {
            if (strncmp(all_lines[i], "--- ", 4) == 0) {
                /* Parse old filename */
                char *p = all_lines[i] + 4;
                /* Skip a/ prefix if present */
                if (p[0] == 'a' && p[1] == '/') p += 2;
                old_name = xstrdup(p);
                i++;
                
                /* Expect +++ line */
                if (i < num_lines && strncmp(all_lines[i], "+++ ", 4) == 0) {
                    p = all_lines[i] + 4;
                    if (p[0] == 'b' && p[1] == '/') p += 2;
                    /* Strip timestamp if present */
                    char *tab = strchr(p, '\t');
                    if (tab) *tab = '\0';
                    new_name = xstrdup(p);
                    i++;
                }
                break;
            }
            i++;
        }
        
        if (!old_name) continue;
        
        /* Collect hunks */
        hunk_t **hunks = NULL;
        int num_hunks = 0;
        int hunk_cap = 0;
        
        while (i < num_lines && strncmp(all_lines[i], "@@", 2) == 0) {
            /* Parse @@ -old_start,old_count +new_start,new_count @@ */
            hunk_t *h = xmalloc(sizeof(hunk_t));
            memset(h, 0, sizeof(*h));
            
            if (sscanf(all_lines[i], "@@ -%d,%d +%d,%d @@", 
                       &h->old_start, &h->old_count, &h->new_start, &h->new_count) < 4) {
                /* Try without count */
                if (sscanf(all_lines[i], "@@ -%d +%d @@", &h->old_start, &h->new_start) >= 2) {
                    h->old_count = 1;
                    h->new_count = 1;
                }
            }
            i++;
            
            /* Collect hunk lines */
            h->lines = xmalloc(sizeof(char*) * 65536);
            h->line_count = 0;
            
            while (i < num_lines && 
                   (all_lines[i][0] == '+' || all_lines[i][0] == '-' || 
                    all_lines[i][0] == ' ' || all_lines[i][0] == '\\')) {
                h->lines[h->line_count++] = xstrdup(all_lines[i]);
                i++;
            }
            
            if (num_hunks >= hunk_cap) {
                hunk_cap = hunk_cap ? hunk_cap * 2 : 4;
                hunks = realloc(hunks, sizeof(hunk_t*) * hunk_cap);
            }
            hunks[num_hunks++] = h;
        }
        
        /* Create file patch */
        if (num_patches >= patch_cap) {
            patch_cap = patch_cap ? patch_cap * 2 : 4;
            patches = realloc(patches, sizeof(file_patch_t*) * patch_cap);
        }
        file_patch_t *fp = xmalloc(sizeof(file_patch_t));
        fp->filename = old_name;  /* Use old filename */
        fp->hunks = hunks;
        fp->hunk_count = num_hunks;
        fp->hunk_cap = hunk_cap;
        patches[num_patches++] = fp;
        
        free(new_name);
    }
    
    /* Free all_lines */
    for (int j = 0; j < num_lines; j++) free(all_lines[j]);
    free(all_lines);
    free(copy);
    
    *patches_out = patches;
    *patch_count = num_patches;
    return 0;
}

/* Apply a single hunk to the file lines */
static int apply_hunk(char ***lines, int *count, hunk_t *hunk, int reverse) {
    /* Find the position to apply the hunk */
    int expected_start = reverse ? hunk->new_start : hunk->old_start;
    
    /* Search for the context match */
    int match_pos = -1;
    
    /* Try to find the hunk context in the file */
    int search_start = expected_start > 0 ? expected_start - 1 : 0;
    
    /* Collect context lines from the hunk (non +/- lines) */
    for (int attempt = 0; attempt < 2; attempt++) {
        int start = (attempt == 0) ? search_start : 0;
        
        for (int i = start; i < *count; i++) {
            int li = 0;
            int match = 1;
            
            /* Check if hunk context matches at this position */
            for (int j = 0; j < hunk->line_count && match; j++) {
                char prefix = reverse ? 
                    (hunk->lines[j][0] == '+' ? '-' : 
                     hunk->lines[j][0] == '-' ? '+' : hunk->lines[j][0]) :
                    hunk->lines[j][0];
                
                if (prefix == ' ') {
                    /* Context line — must match */
                    if (i + li >= *count) { match = 0; break; }
                    /* Compare content (skip prefix char) */
                    const char *hcontent = hunk->lines[j] + 1;
                    const char *fcontent = (*lines)[i + li];
                    size_t hlen = strlen(hcontent);
                    size_t flen = strlen(fcontent);
                    /* Remove trailing newline differences */
                    if (hlen > 0 && hcontent[hlen-1] == '\n') hlen--;
                    if (flen > 0 && fcontent[flen-1] == '\n') flen--;
                    if (hlen != flen || strncmp(hcontent, fcontent, hlen) != 0)
                        match = 0;
                    li++;
                } else if (prefix == '-') {
                    /* Old line — must exist in file */
                    if (i + li >= *count) { match = 0; break; }
                    const char *hcontent = hunk->lines[j] + 1;
                    const char *fcontent = (*lines)[i + li];
                    size_t hlen = strlen(hcontent);
                    size_t flen = strlen(fcontent);
                    if (hlen > 0 && hcontent[hlen-1] == '\n') hlen--;
                    if (flen > 0 && fcontent[flen-1] == '\n') flen--;
                    if (hlen != flen || strncmp(hcontent, fcontent, hlen) != 0)
                        match = 0;
                    li++;
                }
                /* '+' lines are not checked (they're additions) */
            }
            
            if (match) {
                match_pos = i;
                break;
            }
        }
        if (match_pos >= 0) break;
    }
    
    if (match_pos < 0) {
        fprintf(stderr, "%s: hunk failed to apply\n", program_name);
        return -1;
    }
    
    /* Apply the hunk: replace old lines with new lines */
    /* Collect new lines */
    char **new_lines = xmalloc(sizeof(char*) * (hunk->line_count + 1));
    int new_count = 0;
    
    for (int j = 0; j < hunk->line_count; j++) {
        char prefix = reverse ? 
            (hunk->lines[j][0] == '+' ? '-' : 
             hunk->lines[j][0] == '-' ? '+' : hunk->lines[j][0]) :
            hunk->lines[j][0];
        
        if (prefix == ' ' || prefix == '+') {
            /* Context or added line — ensure it ends with newline */
            char *content = hunk->lines[j] + 1;
            size_t clen = strlen(content);
            if (clen == 0 || content[clen-1] != '\n') {
                new_lines[new_count++] = xstrdup(content);
                /* Append newline if missing */
                char *withnl = xmalloc(strlen(content) + 2);
                strcpy(withnl, content);
                strcat(withnl, "\n");
                free(new_lines[new_count - 1]);
                new_lines[new_count - 1] = withnl;
            } else {
                new_lines[new_count++] = xstrdup(content);
            }
        }
    }
    
    /* Count old lines (context + removed) */
    int old_lines_count = 0;
    for (int j = 0; j < hunk->line_count; j++) {
        char prefix = reverse ? 
            (hunk->lines[j][0] == '+' ? '-' : 
             hunk->lines[j][0] == '-' ? '+' : hunk->lines[j][0]) :
            hunk->lines[j][0];
        if (prefix == ' ' || prefix == '-')
            old_lines_count++;
    }
    
    /* Replace old_lines_count lines at match_pos with new_count lines */
    int total = *count - old_lines_count + new_count;
    char **result = xmalloc(sizeof(char*) * (total + 1));
    int ri = 0;
    
    /* Copy lines before match */
    for (int j = 0; j < match_pos; j++)
        result[ri++] = (*lines)[j];
    
    /* Insert new lines */
    for (int j = 0; j < new_count; j++)
        result[ri++] = new_lines[j];
    
    /* Copy lines after the replaced section */
    for (int j = match_pos + old_lines_count; j < *count; j++)
        result[ri++] = (*lines)[j];
    
    /* Free old lines that were replaced */
    for (int j = match_pos; j < match_pos + old_lines_count; j++)
        free((*lines)[j]);
    
    free(new_lines);
    free(*lines);
    *lines = result;
    *count = ri;
    
    return 0;
}

static int apply_file_patch(file_patch_t *fp, int strip, int reverse, int dry_run) {
    char *target = strip_prefix(fp->filename, strip);
    
    /* Read original file */
    int file_count = 0;
    char **file_lines = read_file_lines(target, &file_count);
    if (!file_lines) {
        /* File doesn't exist — might be a new file creation patch */
        if (fp->hunk_count > 0 && fp->hunks[0]->line_count > 0) {
            /* Check if all lines are additions */
            int all_add = 1;
            for (int i = 0; i < fp->hunks[0]->line_count; i++) {
                char prefix = reverse ? 
                    (fp->hunks[0]->lines[i][0] == '+' ? '-' :
                     fp->hunks[0]->lines[i][0] == '-' ? '+' : fp->hunks[0]->lines[i][0]) :
                    fp->hunks[0]->lines[i][0];
                if (prefix != '+') { all_add = 0; break; }
            }
            if (all_add) {
                file_lines = xmalloc(sizeof(char*));
                file_count = 0;
            } else {
                fprintf(stderr, "%s: can't find file to patch: %s\n", program_name, target);
                free(target);
                return 1;
            }
        } else {
            fprintf(stderr, "%s: can't find file to patch: %s\n", program_name, target);
            free(target);
            return 1;
        }
    }
    
    /* Apply each hunk */
    int ok = 0;
    int fail = 0;
    
    for (int i = 0; i < fp->hunk_count; i++) {
        if (apply_hunk(&file_lines, &file_count, fp->hunks[i], reverse) == 0) {
            ok++;
        } else {
            fail++;
        }
    }
    
    if (fail > 0) {
        fprintf(stderr, "%s: %d hunk(s) FAILED to apply\n", program_name, fail);
    }
    
    if (!dry_run && fail == 0) {
        if (file_count == 0) {
            /* Empty file — remove it */
            unlink(target);
        } else {
            write_file_lines(target, file_lines, file_count);
        }
        printf("%s: patching file %s (%d hunk%s applied)\n", program_name, target, ok, ok == 1 ? "" : "s");
    } else if (dry_run) {
        printf("%s: checking file %s (%d hunk%s would apply)\n", program_name, target, ok, ok == 1 ? "" : "s");
    }
    
    free_lines(file_lines, file_count);
    free(target);
    return fail > 0 ? 1 : 0;
}

static void usage(void) {
    printf(
        "patch — apply diff patches (meuos-utils)\n"
        "\n"
        "usage: patch [options] [patchfile]\n"
        "\n"
        "options:\n"
        "  -p N       strip N path components\n"
        "  -i FILE    read patch from FILE\n"
        "  -o FILE    output to FILE\n"
        "  -R         reverse patch\n"
        "  -E         remove empty files\n"
        "  --dry-run  test only, don't modify files\n"
        "  --help     show help\n"
        "  --version  show version\n");
}

int main(int argc, char **argv) {
    int strip = 0;
    int reverse = 0;
    int dry_run = 0;
    const char *patchfile = NULL;
    const char *output_file = NULL;
    int oi = 1;
    
    while (oi < argc) {
        const char *opt = argv[oi];
        if (strcmp(opt, "--help") == 0) { usage(); return 0; }
        if (strcmp(opt, "--version") == 0) { printf("patch (meuos-utils)\n"); return 0; }
        if (strcmp(opt, "--dry-run") == 0) { dry_run = 1; oi++; continue; }
        if (strcmp(opt, "-R") == 0 || strcmp(opt, "--reverse") == 0) { reverse = 1; oi++; continue; }
        if (strcmp(opt, "-E") == 0) { oi++; continue; }
        if (strcmp(opt, "-i") == 0) {
            if (++oi >= argc) { fprintf(stderr, "%s: -i requires argument\n", program_name); return 2; }
            patchfile = argv[oi]; oi++; continue;
        }
        if (strcmp(opt, "-o") == 0) {
            if (++oi >= argc) { fprintf(stderr, "%s: -o requires argument\n", program_name); return 2; }
            output_file = argv[oi]; oi++; continue;
        }
        if (strcmp(opt, "-p") == 0) {
            if (++oi >= argc) { fprintf(stderr, "%s: -p requires argument\n", program_name); return 2; }
            strip = atoi(argv[oi]); oi++; continue;
        }
        if (strncmp(opt, "-p", 2) == 0 && isdigit(opt[2])) {
            strip = atoi(opt + 2); oi++; continue;
        }
        /* Non-option argument: patchfile */
        if (opt[0] != '-') {
            patchfile = opt;
            oi++;
            continue;
        }
        fprintf(stderr, "%s: unknown option %s\n", program_name, opt);
        return 2;
    }
    
    /* Read patch data */
    char *patchdata;
    size_t patchlen;
    
    if (patchfile) {
        FILE *f = fopen(patchfile, "r");
        if (!f) { fprintf(stderr, "%s: %s: %s\n", program_name, patchfile, strerror(errno)); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        patchdata = xmalloc(sz + 1);
        patchlen = fread(patchdata, 1, sz, f);
        patchdata[patchlen] = '\0';
        fclose(f);
    } else {
        /* Read from stdin */
        size_t cap = 65536;
        patchdata = xmalloc(cap);
        patchlen = 0;
        int c;
        while ((c = fgetc(stdin)) != EOF) {
            if (patchlen >= cap) {
                cap *= 2;
                patchdata = realloc(patchdata, cap);
            }
            patchdata[patchlen++] = (char)c;
        }
    }
    
    /* Parse patch */
    file_patch_t **patches;
    int num_patches;
    parse_patch(patchdata, patchlen, &patches, &num_patches);
    
    if (num_patches == 0) {
        fprintf(stderr, "%s: no patches found\n", program_name);
        free(patchdata);
        return 1;
    }
    
    int ret = 0;
    for (int i = 0; i < num_patches; i++) {
        if (apply_file_patch(patches[i], strip, reverse, dry_run) != 0)
            ret = 1;
    }
    
    free(patchdata);
    return ret;
}
