/* jsonfmt.c — TUI JSON 格式化/查看器
 *
 * 功能：读取 JSON 文本，带语法高亮地树形显示，支持折叠浏览。
 * 输入：文件参数 / stdin 管道 / 内置示例
 *
 * 用法:
 *   jsonfmt file.json           # 查看文件
 *   echo '{"a":1}' | jsonfmt    # 从管道
 *   jsonfmt                     # 内置示例
 *
 * 按键:
 *   Up/Down  滚动    Enter  折叠/展开    +/-  全部展开/折叠
 *   q/ESC   退出     Home/End  跳首/跳尾
 *
 * 编译: 需要 libtui.a（Makefile 特殊链接规则）
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "meuos/libtui.h"
#include "meuos/utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════
 *  简易 JSON 解析器
 * ══════════════════════════════════════════════════════ */

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } json_type_t;

typedef struct json_node {
    json_type_t type;
    char        key[128];
    char        str_val[512];
    struct json_node *children;
    int         n_children;
    int         folded;
} json_node_t;

typedef struct { const char *src; int pos; int len; } json_parser_t;

static void skip_ws(json_parser_t *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c==' '||c=='\t'||c=='\n'||c=='\r') p->pos++; else break;
    }
}

static json_node_t *parse_value(json_parser_t *p);

static char *parse_string_raw(json_parser_t *p, char *out, int max) {
    skip_ws(p);
    if (p->pos>=p->len || p->src[p->pos]!='"') return NULL;
    p->pos++;
    int i=0;
    while (p->pos<p->len && p->src[p->pos]!='"') {
        char c=p->src[p->pos++];
        if (c=='\\' && p->pos<p->len) {
            char e=p->src[p->pos++];
            switch(e){case'n':c='\n';break;case't':c='\t';break;case'r':c='\r';break;case'"':c='"';break;case'\\':c='\\';break;case'/':c='/';break;default:c=e;}
        }
        if (i<max-1) out[i++]=c;
    }
    if (p->pos<p->len) p->pos++;
    out[i]='\0';
    return out;
}

static json_node_t *parse_object(json_parser_t *p) {
    p->pos++; skip_ws(p);
    json_node_t *nodes=NULL; int count=0, cap=0;
    while (p->pos<p->len && p->src[p->pos]!='}') {
        skip_ws(p);
        if (p->src[p->pos]=='}') break;
        char key[128];
        if (!parse_string_raw(p,key,sizeof(key))) break;
        skip_ws(p);
        if (p->pos<p->len && p->src[p->pos]==':') p->pos++;
        json_node_t *val=parse_value(p);
        if (!val) break;
        size_t klen=strlen(key);
        if (klen>=sizeof(val->key)) klen=sizeof(val->key)-1;
        memcpy(val->key,key,klen); val->key[klen]='\0';
        if (count>=cap){cap=cap?cap*2:4;nodes=realloc(nodes,(size_t)cap*sizeof(json_node_t));}
        nodes[count++]=*val; free(val);
        skip_ws(p);
        if (p->pos<p->len && p->src[p->pos]==',') p->pos++;
    }
    if (p->pos<p->len) p->pos++;
    json_node_t *node=calloc(1,sizeof(json_node_t));
    node->type=JSON_OBJECT; node->children=nodes; node->n_children=count;
    return node;
}

static json_node_t *parse_array(json_parser_t *p) {
    p->pos++; skip_ws(p);
    json_node_t *nodes=NULL; int count=0, cap=0;
    while (p->pos<p->len && p->src[p->pos]!=']') {
        skip_ws(p);
        if (p->src[p->pos]==']') break;
        json_node_t *val=parse_value(p);
        if (!val) break;
        snprintf(val->key,sizeof(val->key),"[%d]",count);
        if (count>=cap){cap=cap?cap*2:4;nodes=realloc(nodes,(size_t)cap*sizeof(json_node_t));}
        nodes[count++]=*val; free(val);
        skip_ws(p);
        if (p->pos<p->len && p->src[p->pos]==',') p->pos++;
    }
    if (p->pos<p->len) p->pos++;
    json_node_t *node=calloc(1,sizeof(json_node_t));
    node->type=JSON_ARRAY; node->children=nodes; node->n_children=count;
    return node;
}

static json_node_t *parse_value(json_parser_t *p) {
    skip_ws(p);
    if (p->pos>=p->len) return NULL;
    char c=p->src[p->pos];
    json_node_t *node=calloc(1,sizeof(json_node_t));
    if (c=='{'){free(node);return parse_object(p);}
    if (c=='['){free(node);return parse_array(p);}
    if (c=='"'){node->type=JSON_STRING;parse_string_raw(p,node->str_val,sizeof(node->str_val));return node;}
    if (c=='t'||c=='f'){node->type=JSON_BOOL;
        if(p->pos+4<=p->len&&strncmp(p->src+p->pos,"true",4)==0){strcpy(node->str_val,"true");p->pos+=4;}
        else if(p->pos+5<=p->len&&strncmp(p->src+p->pos,"false",5)==0){strcpy(node->str_val,"false");p->pos+=5;}
        return node;}
    if (c=='n'){node->type=JSON_NULL;
        if(p->pos+4<=p->len&&strncmp(p->src+p->pos,"null",4)==0){strcpy(node->str_val,"null");p->pos+=4;}
        return node;}
    int start=p->pos;
    while(p->pos<p->len){char ch=p->src[p->pos];
        if((ch>='0'&&ch<='9')||ch=='-'||ch=='.'||ch=='e'||ch=='E'||ch=='+')p->pos++;else break;}
    int n=p->pos-start;
    if(n>0&&n<(int)sizeof(node->str_val)){memcpy(node->str_val,p->src+start,(size_t)n);node->str_val[n]='\0';node->type=JSON_NUMBER;}
    return node;
}

static json_node_t *json_parse(const char *src, int len) {
    json_parser_t p={.src=src,.pos=0,.len=len};
    return parse_value(&p);
}

static void json_free(json_node_t *node) {
    if (!node) return;
    if (node->children) {
        for (int i=0;i<node->n_children;i++) json_free(&node->children[i]);
        free(node->children);
    }
    free(node);
}

/* ══════════════════════════════════════════════════════
 *  树形扁平化
 * ══════════════════════════════════════════════════════ */

typedef struct { json_node_t *node; int depth; int is_last; char prefix[256]; } flat_line_t;

#define MAX_FLAT 512
static flat_line_t flat_lines[MAX_FLAT];
static int flat_count = 0;

static void flatten(json_node_t *node, int depth, int is_last, const char *prefix) {
    if (!node || flat_count>=MAX_FLAT) return;
    flat_lines[flat_count].node=node;
    flat_lines[flat_count].depth=depth;
    flat_lines[flat_count].is_last=is_last;
    size_t plen=strlen(prefix);
    if(plen>=sizeof(flat_lines[flat_count].prefix))plen=sizeof(flat_lines[flat_count].prefix)-1;
    memcpy(flat_lines[flat_count].prefix,prefix,plen);
    flat_lines[flat_count].prefix[plen]='\0';
    flat_count++;
    if ((node->type==JSON_OBJECT||node->type==JSON_ARRAY)&&!node->folded&&node->n_children>0) {
        char cp[256];
        for (int i=0;i<node->n_children;i++) {
            int cl=(i==node->n_children-1);
            snprintf(cp,sizeof(cp),"%s%s",prefix,is_last?"  ":"| ");
            flatten(&node->children[i],depth+1,cl,cp);
        }
    }
}

static void rebuild_flat(json_node_t *root) { flat_count=0; flatten(root,0,1,""); }

static tui_color_t value_color(json_type_t t) {
    switch(t){case JSON_STRING:return TUI_COLOR_GREEN;case JSON_NUMBER:return TUI_COLOR_YELLOW;
    case JSON_BOOL:return TUI_COLOR_MAGENTA;case JSON_NULL:return TUI_COLOR_RED;
    case JSON_OBJECT:return TUI_COLOR_CYAN;case JSON_ARRAY:return TUI_COLOR_BLUE;default:return TUI_COLOR_DEFAULT;}
}

/* ══════════════════════════════════════════════════════
 *  渲染 + 交互
 * ══════════════════════════════════════════════════════ */

typedef struct { json_node_t *root; int scroll; int cursor; } json_view_t;

static int json_render(int fd, const tui_rect_t *area, void *udata) {
    json_view_t *view=(json_view_t *)udata;
    tui_rect_t inner=*area;
    tui_draw_border(fd,&inner,"  JSON Viewer  ",0,tui_meuos_theme.border);
    if (!tui_rect_valid(&inner)) return TUI_OK;

    int max_lines=inner.rows;
    int start=view->scroll;
    if (start<0)start=0;
    if (start>flat_count-max_lines&&flat_count>max_lines)start=flat_count-max_lines;
    if (start<0)start=0;
    int visible=flat_count-start;
    if (visible>max_lines)visible=max_lines;

    for (int i=0;i<visible;i++) {
        int idx=start+i;
        if (idx>=flat_count)break;
        flat_line_t *fl=&flat_lines[idx];
        json_node_t *node=fl->node;
        int is_cur=(idx==view->cursor);
        tui_cursor_goto(fd,inner.row+i,inner.col);

        if (is_cur){tui_set_bg(fd,tui_meuos_theme.highlight);tui_set_fg(fd,TUI_COLOR_WHITE);tui_set_attr(fd,TUI_ATTR_BOLD);tui_spaces(fd,inner.cols);tui_cursor_goto(fd,inner.row+i,inner.col);}
        if (!is_cur)tui_reset_style(fd);
        tui_set_fg(fd,tui_meuos_theme.dim);tui_set_attr(fd,TUI_ATTR_DIM);
        if(is_cur){tui_set_bg(fd,tui_meuos_theme.highlight);}
        tui_write(fd,fl->prefix);

        if (is_cur){tui_set_bg(fd,tui_meuos_theme.highlight);tui_set_fg(fd,TUI_COLOR_YELLOW);tui_set_attr(fd,TUI_ATTR_BOLD);}
        else{tui_set_fg(fd,TUI_COLOR_YELLOW);tui_set_attr(fd,TUI_ATTR_BOLD);}
        tui_write(fd,node->folded?"> ":"v ");

        if(!is_cur)tui_reset_style(fd);
        if (node->key[0]){
            if(is_cur){tui_set_bg(fd,tui_meuos_theme.highlight);tui_set_fg(fd,TUI_COLOR_CYAN);}
            else tui_set_fg(fd,TUI_COLOR_CYAN);
            tui_write(fd,node->key);
            if(is_cur)tui_set_bg(fd,tui_meuos_theme.highlight);
            tui_write(fd,": ");
        }
        if(!is_cur)tui_reset_style(fd);
        tui_color_t vc=value_color(node->type);
        if(is_cur){tui_set_bg(fd,tui_meuos_theme.highlight);tui_set_fg(fd,vc);tui_set_attr(fd,TUI_ATTR_BOLD);}
        else tui_set_fg(fd,vc);
        switch(node->type){
        case JSON_STRING:tui_write(fd,"\"");{int b=tui_truncate(node->str_val,inner.cols-20);write(fd,node->str_val,(size_t)b);}tui_write(fd,"\"");break;
        case JSON_NUMBER:case JSON_BOOL:case JSON_NULL:tui_write(fd,node->str_val);break;
        case JSON_OBJECT:{if(node->folded){char s[32];snprintf(s,sizeof(s),"{...} (%d)",node->n_children);tui_write(fd,s);}else tui_write(fd,"{");}break;
        case JSON_ARRAY:{if(node->folded){char s[32];snprintf(s,sizeof(s),"[...] (%d)",node->n_children);tui_write(fd,s);}else tui_write(fd,"[");}break;
        }
        tui_reset_style(fd);
    }
    for (int i=visible;i<max_lines;i++){tui_cursor_goto(fd,inner.row+i,inner.col);tui_clear_eol(fd);}
    return TUI_OK;
}

static const char *example_json =
    "{\"project\":\"MeuOS Kit\",\"version\":\"1.0.0\",\"license\":\"RFL v1.0\","
    "\"components\":{\"compiler\":\"mcc\",\"libc\":\"meuos-libc\","
    "\"build_system\":\"meow\",\"shell\":\"msh\","
    "\"toolchain\":[\"as\",\"ld\",\"ar\",\"nm\",\"readelf\",\"strip\",\"objcopy\",\"objdump\"]},"
    "\"features\":[\"self-bootstrapping\",\"zero-gnu\",\"zero-llvm\",\"posix-compliant\"],"
    "\"architectures\":[\"x86_64\",\"aarch64\",\"riscv64\",\"i386\",\"loongarch64\",\"arm\"],"
    "\"active\":true,\"kernel_ready\":false}";

static const char *usage_text =
    "Usage: jsonfmt [FILE]\n"
    "  View JSON files with syntax highlighting and tree navigation.\n"
    "  If no FILE given, reads from stdin (or shows built-in example).\n\n"
    "  --help     Show this help\n"
    "  --version  Show version\n";

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) utils_usage(usage_text);

    /* 读取 JSON */
    char *json_text = NULL;
    int json_len = 0;

    if (argi < argc) {
        FILE *f = fopen(argv[argi], "r");
        if (!f) die("cannot open: %s", argv[argi]);
        fseek(f, 0, SEEK_END);
        json_len = (int)ftell(f);
        fseek(f, 0, SEEK_SET);
        json_text = xmalloc((size_t)json_len + 1);
        fread(json_text, 1, (size_t)json_len, f);
        json_text[json_len] = '\0';
        fclose(f);
    } else if (!isatty(0)) {
        char buf[4096]; int total = 0;
        json_text = xmalloc(4096);
        while (!feof(stdin)) {
            int n = (int)fread(buf, 1, sizeof(buf), stdin);
            if (n <= 0) break;
            json_text = xrealloc(json_text, (size_t)(total + n + 1));
            memcpy(json_text + total, buf, (size_t)n);
            total += n;
        }
        json_text[total] = '\0';
        json_len = total;
        if (json_len == 0) { json_text = xstrdup(example_json); json_len = (int)strlen(example_json); }
    } else {
        json_text = xstrdup(example_json);
        json_len = (int)strlen(example_json);
    }

    json_node_t *root = json_parse(json_text, json_len);
    if (!root) die("JSON parse error");

    json_view_t view = { .root = root, .scroll = 0, .cursor = 0 };
    rebuild_flat(root);

    /* TUI 事件循环 */
    tui_raw_mode(0, 1);
    tui_alt_screen(0, 1);
    tui_clear_screen(0);
    tui_cursor_show(0, 0);

    tui_size_t scr;
    if (tui_get_size(0, &scr) != TUI_OK) { scr.rows = 30; scr.cols = 80; }

    tui_event_t ev;
    int running = 1;

    while (running) {
        /* 标题栏 */
        tui_cursor_goto(0, 1, 1);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_set_attr(0, TUI_ATTR_BOLD);
        tui_spaces(0, scr.cols - 1);
        tui_cursor_goto(0, 1, 3);
        tui_set_fg(0, TUI_COLOR_WHITE);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_write(0, "JSON Viewer — MeuOS Kit");
        const char *hint = " q=quit  Up/Down=scroll  Enter=fold  +=expand  -=collapse ";
        int hw = tui_strwidth(hint);
        tui_cursor_goto(0, 1, scr.cols - hw - 1);
        tui_set_fg(0, TUI_COLOR_YELLOW);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_write(0, hint);
        tui_reset_style(0);

        /* JSON 视图 */
        tui_rect_t ja = { 3, 1, scr.rows - 5, scr.cols - 1 };
        json_render(0, &ja, &view);

        /* 状态栏 */
        tui_cursor_goto(0, scr.rows, 1);
        tui_set_bg(0, tui_meuos_theme.accent);
        tui_set_fg(0, TUI_COLOR_WHITE);
        tui_set_attr(0, TUI_ATTR_BOLD);
        char status[256];
        snprintf(status, sizeof(status), " Lines: %d | Cursor: %d ", flat_count, view.cursor);
        tui_write(0, status);
        const char *sr = " libtui v1.0 ";
        int rw = tui_strwidth(sr);
        int pad = scr.cols - 1 - tui_strwidth(status) - rw;
        if (pad > 0) tui_spaces(0, pad);
        tui_write(0, sr);
        tui_reset_style(0);

        if (tui_getkey(0, &ev) == TUI_OK) {
            switch ((int)ev.key) {
            case 'q': case TUI_KEY_ESC: running = 0; break;
            case TUI_KEY_UP: if (view.cursor > 0) { view.cursor--; if (view.cursor < view.scroll) view.scroll = view.cursor; } break;
            case TUI_KEY_DOWN: if (view.cursor < flat_count - 1) { view.cursor++; if (view.cursor >= view.scroll + scr.rows - 7) view.scroll = view.cursor - (scr.rows - 7) + 1; } break;
            case TUI_KEY_CR: case TUI_KEY_LF:
                if (view.cursor < flat_count) {
                    json_node_t *n = flat_lines[view.cursor].node;
                    if (n->type == JSON_OBJECT || n->type == JSON_ARRAY) { n->folded = !n->folded; rebuild_flat(root); }
                }
                break;
            case '+': for (int i = 0; i < flat_count; i++) { if (flat_lines[i].node->type == JSON_OBJECT || flat_lines[i].node->type == JSON_ARRAY) flat_lines[i].node->folded = 0; } rebuild_flat(root); break;
            case '-': for (int i = 0; i < flat_count; i++) { if (flat_lines[i].node->type == JSON_OBJECT || flat_lines[i].node->type == JSON_ARRAY) flat_lines[i].node->folded = 1; } if (root->type == JSON_OBJECT || root->type == JSON_ARRAY) root->folded = 0; rebuild_flat(root); break;
            case TUI_KEY_HOME: view.cursor = 0; view.scroll = 0; break;
            case TUI_KEY_END: view.cursor = flat_count - 1; break;
            default: break;
            }
        }
    }

    tui_cursor_show(0, 1);
    tui_clear_screen(0);
    tui_alt_screen(0, 0);
    tui_raw_mode(0, 0);
    json_free(root);
    free(json_text);
    return 0;
}
