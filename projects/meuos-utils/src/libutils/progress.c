/* libutils/progress.c — TTY 进度条 */

#define _POSIX_C_SOURCE 199309L

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "meuos/progress.h"
#include "meuos/utils.h"

struct progress {
    char label[64];
    uint64_t total;
    uint64_t current;
    int width;
    int indeterminate;
    struct timespec started;
    struct timespec last_update;
};

static double elapsed_sec(const progress_t *p) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - p->started.tv_sec)
         + (now.tv_nsec - p->started.tv_nsec) * 1e-9;
}

static int is_tty(void) {
    return isatty(fileno(stderr));  /* 进度条写 stderr，不污染 stdout */
}

progress_t *progress_new(const char *label, uint64_t total) {
    progress_t *p = xcalloc(1, sizeof(*p));
    if (label) {
        strncpy(p->label, label, sizeof(p->label) - 1);
    }
    p->total = total;
    p->indeterminate = (total == 0);
    p->width = 32;
    clock_gettime(CLOCK_MONOTONIC, &p->started);
    p->last_update = p->started;
    return p;
}

void progress_update(progress_t *p, uint64_t current) {
    if (!p) return;
    p->current = current;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double since_ms = (now.tv_sec - p->last_update.tv_sec) * 1000.0
                    + (now.tv_nsec - p->last_update.tv_nsec) * 1e-6;
    if (since_ms < 100.0) return;  /* 限制刷新频率 10 fps */
    p->last_update = now;
    progress_force_flush(p);
}

static void render_bar(FILE *fp, int width, double ratio) {
    int filled = (int)(ratio * width);
    if (filled > width) filled = width;
    fputc('|', fp);
    for (int i = 0; i < width; i++) {
        if (i < filled) fputs("\xe2\x96\x88", fp);  /* █ */
        else fputs("\xe2\x96\x91", fp);             /* ░ */
    }
    fputc('|', fp);
}

void progress_force_flush(progress_t *p) {
    if (!p) return;
    FILE *fp = stderr;

    if (!is_tty()) {
        /* 非 tty：每行输出 "label: X / Y" */
        fprintf(fp, "%s: %lu / %lu\r",
                p->label, (unsigned long)p->current,
                (unsigned long)p->total);
        fflush(fp);
        return;
    }

    double ratio = p->indeterminate
                 ? 0.0
                 : (p->total ? (double)p->current / (double)p->total : 0.0);
    if (ratio > 1.0) ratio = 1.0;
    fprintf(fp, "\r%s ", p->label);
    render_bar(fp, p->width, ratio);

    if (p->indeterminate) {
        fprintf(fp, " ? ");
    } else {
        fprintf(fp, " %3d%% ", (int)(ratio * 100));
    }

    /* ETA */
    double elapsed = elapsed_sec(p);
    if (!p->indeterminate && ratio > 0.05) {
        double eta = elapsed * (1.0 - ratio) / ratio;
        if (eta < 60) fprintf(fp, "ETA %4.0fs   ", eta);
        else if (eta < 3600) fprintf(fp, "ETA %3.0fm   ", eta / 60);
        else fprintf(fp, "ETA %3.0fh   ", eta / 3600);
    } else {
        fprintf(fp, "          ");
    }
    fflush(fp);
}

void progress_finish(progress_t *p) {
    if (!p) return;
    if (!p->indeterminate && p->total) {
        progress_update(p, p->total);
    }
    progress_force_flush(p);
    if (is_tty()) fprintf(stderr, "\n");
}

void progress_free(progress_t *p) {
    if (!p) return;
    free(p);
}
