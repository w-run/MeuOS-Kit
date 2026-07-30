/* bench_mxa.c — MxA 容器性能基准 */
#include "mxa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

static double now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1e6 + (double)tv.tv_usec;
}

static char *td_small, *td_med, *td_large;

static void init_data(void) {
    td_small = malloc(1024);
    for (int i = 0; i < 1024; i++) td_small[i] = (char)(rand() & 0xFF);
    td_med = malloc(100000);
    for (int i = 0; i < 100000; i++) td_med[i] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[i % 26];
    td_large = malloc(1000000);
    for (int i = 0; i < 1000000; i++)
        td_large[i] = (i < 500000) ? (char)(rand() & 0xFF) : 'A' + (i % 26);
}

static void bench(int lv, const void *d, size_t sz, const char *label) {
    struct mxa_params p; memset(&p, 0, sizeof(p)); p.level = lv;
    double t0 = now_us();
    void *ctx; mxa_create(&ctx, &p);
    mxa_add_file(ctx, "t.bin", d, sz, 0644, 0,0,0);
    void *r; size_t rl; mxa_finish(ctx, &r, &rl);
    double t1 = now_us();
    void *rc; mxa_open(r, rl, &rc);
    void *dd; size_t dl; mxa_read_file(rc, "t.bin", &dd, &dl);
    mxa_close(rc);
    double t2 = now_us();
    int ok = (dl == sz) && memcmp(dd, d, sz) == 0;
    printf("  lv%-2d  %7zu -> %7zu (%6.2f%%)  enc:%6.0fus  dec:%6.0fus  %s\n",
           lv, sz, rl, (double)rl/sz*100, t1-t0, t2-t1, ok?"OK":"FAIL");
    free(r); free(dd);
}

int main(void) {
    printf("=== MxA Container Benchmark ===\n\n");
    init_data();
    struct { const char *l; char *d; size_t s; } ts[] = {
        {"1KB random",   td_small, 1024},
        {"100KB text",   td_med,   100000},
        {"1MB mixed",    td_large, 1000000},
    };
    for (int t = 0; t < 3; t++) {
        printf("--- %s ---\n", ts[t].l);
        bench(1, ts[t].d, ts[t].s, ts[t].l);
        bench(3, ts[t].d, ts[t].s, ts[t].l);
        bench(6, ts[t].d, ts[t].s, ts[t].l);
        bench(9, ts[t].d, ts[t].s, ts[t].l);
        /* 5-file */
        struct mxa_params p; memset(&p,0,sizeof(p)); p.level=6;
        double t0=now_us(); void *ctx; mxa_create(&ctx,&p);
        size_t ch=ts[t].s/5;
        for(int i=0;i<5;i++){char n[32];snprintf(n,32,"p%d.bin",i);mxa_add_file(ctx,n,(const char*)ts[t].d+i*ch,i<4?ch:ts[t].s-i*ch,0644,0,0,0);}
        void *r;size_t rl;mxa_finish(ctx,&r,&rl);
        double t1=now_us(); printf("  5-file  %7zu -> %7zu (%6.2f%%)  enc:%6.0fus\n",ts[t].s,rl,(double)rl/ts[t].s*100,t1-t0);
        free(r);
        printf("\n");
    }
    /* Crypto overhead */
    printf("--- Crypto overhead (100KB text, lv6) ---\n");
    uint8_t key[32],sk[32]; memset(key,0x42,32); memset(sk,0x24,32);
    double t0=now_us();
    struct mxa_params p; memset(&p,0,sizeof(p)); p.level=6;
    void *ctx; mxa_create(&ctx,&p);
    mxa_add_file(ctx,"p.bin",td_med,100000,0644,0,0,0);
    void *r;size_t rl;mxa_finish(ctx,&r,&rl);
    double t1=now_us(); printf("  No crypto:    %6.0fus (%zu bytes)\n",t1-t0,rl); free(r);
    memset(&p,0,sizeof(p)); p.level=6; p.key=key; p.flags=MXA_FLAG_ENCRYPTED;
    mxa_create(&ctx,&p); mxa_add_file(ctx,"e.bin",td_med,100000,0644,0,0,0);
    mxa_finish(ctx,&r,&rl); double t2=now_us(); printf("  Encrypted:    %6.0fus (%zu bytes)\n",t2-t1,rl); free(r);
    memset(&p,0,sizeof(p)); p.level=6; p.sk=sk; p.flags=MXA_FLAG_SIGNED;
    mxa_create(&ctx,&p); mxa_add_file(ctx,"s.bin",td_med,100000,0644,0,0,0);
    mxa_finish(ctx,&r,&rl); double t3=now_us(); printf("  Signed:       %6.0fus (%zu bytes)\n",t3-t2,rl); free(r);
    memset(&p,0,sizeof(p)); p.level=6; p.key=key; p.sk=sk;
    p.flags=MXA_FLAG_ENCRYPTED|MXA_FLAG_SIGNED;
    mxa_create(&ctx,&p); mxa_add_file(ctx,"b.bin",td_med,100000,0644,0,0,0);
    mxa_finish(ctx,&r,&rl); double t4=now_us(); printf("  Enc+Signed:   %6.0fus (%zu bytes)\n",t4-t3,rl); free(r);
    printf("\n=== Benchmark complete ===\n");
    free(td_small); free(td_med); free(td_large);
    return 0;
}
