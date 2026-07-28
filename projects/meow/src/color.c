/* meow - ANSI color helpers.
 *
 * Lightweight terminal coloring that auto-disables when stdout is not a
 * terminal or when NO_COLOR is set in the environment.  Functions return the
 * original string verbatim when coloring is disabled, so callers can always
 * wrap their text safely. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "meow.h"

enum output_mode g_output_mode = OUTPUT_NORMAL;
int g_color_enabled = 0;

/* Internal scratch buffers (single-threaded use only). */
static char color_buf[16][1024];
static int color_buf_idx = 0;

void
color_init(void)
{
    g_color_enabled = isatty(STDOUT_FILENO) ? 1 : 0;
    if (getenv("NO_COLOR") != NULL)
        g_color_enabled = 0;
    if (g_output_mode == OUTPUT_JSON)
        g_color_enabled = 0;
}

static const char *
wrap(const char *code, const char *text)
{
    if (!g_color_enabled || !text)
        return text;
    char *buf = color_buf[color_buf_idx];
    color_buf_idx = (color_buf_idx + 1) % 16;
    snprintf(buf, sizeof(color_buf[0]), "\033[%sm%s\033[0m", code, text);
    return buf;
}

const char *
color_red(const char *text)
{
    return wrap("31", text);
}

const char *
color_green(const char *text)
{
    return wrap("32", text);
}

const char *
color_yellow(const char *text)
{
    return wrap("33", text);
}

const char *
color_cyan(const char *text)
{
    return wrap("36", text);
}

const char *
color_gray(const char *text)
{
    return wrap("90", text);
}

const char *
color_bold(const char *text)
{
    return wrap("1", text);
}

/* Unified message function.  Levels are filtered by the active output mode. */
void
meow_msg(int level, const char *fmt, ...)
{
    /* Determine whether to emit based on level and mode. */
    int show = 1;
    switch (level) {
    case MSG_ERROR:
    case MSG_WARN:
    case MSG_SUCCESS:
        show = 1;
        break;
    case MSG_INFO:
        show = (g_output_mode != OUTPUT_QUIET);
        break;
    case MSG_DEBUG:
        show = (g_output_mode == OUTPUT_DEBUG);
        break;
    default:
        show = 1;
    }
    if (!show)
        return;

    /* Prefix with level coloring for ERROR/WARN/SUCCESS. */
    const char *tag = NULL;
    if (level == MSG_ERROR)
        tag = color_red("✘");
    else if (level == MSG_WARN)
        tag = color_yellow("⚠");
    else if (level == MSG_SUCCESS)
        tag = color_green("✔");

    va_list ap;
    va_start(ap, fmt);
    if (tag)
        fprintf(stdout, "%s ", tag);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}
