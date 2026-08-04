#!/bin/sh
# rtld_dtv.sh - P0.3 stage-A host harness for rtld dlopen + DTV.
#
# Builds a host test that #includes rtld.c + syscall.c and drives
# rtld_dlopen / rtld_tls_get_addr directly against a real on-disk .so
# with a PT_TLS module, verifying:
#   - rtld_dlopen loads the DSO and registers its PT_TLS module
#   - the module's DTV slot starts NULL (lazy allocation)
#   - a GD TLS access through __tls_get_addr lazily allocates the block,
#     copying .tdata (tls_foo == 42) and zero-initialising .tbss
#   - writes persist in the per-thread TLS block
# The .so is built with the host C compiler so its PT_TLS vaddr is laid
# out consistently (rtld reads the on-disk layout exactly).
set -eu
wt=${1:?worktree path}
rtld_dir="$wt/projects/meuos-toolchain/src/rtld"
work=$(mktemp -d /tmp/meuos-rtld-dtv.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/tlslib.c" <<'CEOF'
_Thread_local int tls_foo = 42;
_Thread_local long tls_big[4];
int tls_get_foo(void) { return tls_foo; }
int tls_set_foo(int v) { tls_foo = v; return tls_foo; }
CEOF
gcc -fPIC -shared -Wl,--hash-style=sysv -o "$work/hosttlslib.so" "$work/tlslib.c"

# Build the harness by splicing the actual rtld sources in.
{
cat <<'HEOF'
#include <stdio.h>
#include <string.h>
#define rtld_main rtld_main_unused_here
HEOF
echo "#include \"$rtld_dir/rtld.c\""
echo "#undef rtld_main"
echo "#include \"$rtld_dir/syscall.c\""
cat <<'HEOF'
int main(int argc, char **argv) {
    struct rtld_state st; memset(&st,0,sizeof(st));
    st.page_size=4096;
    st.lib_count=1; st.libs[0].name="(main)"; st.libs[0].is_main=1;
    static unsigned char tlsarea[4096] __attribute__((aligned(64)));
    st.tls_tp=(uintptr_t)tlsarea+sizeof(tlsarea);
    st.dtv=0;
    rtld_heap_init();
    /* main executable has a static TLS block = module 1 */
    {
        uintptr_t mb=st.tls_tp-32;
        for(int i=0;i<32;i++) ((unsigned char*)mb)[i]=0;
        struct rtld_lib *m0=&st.libs[0];
        m0->tls_modid=1; m0->tls_image=mb; m0->tls_memsz=32;
        m0->tls_filesz=0; m0->tls_align=8;
        st.tls_mods[0].modid=1; st.tls_mods[0].lib_idx=0;
        st.tls_mods[0].template=0; st.tls_mods[0].tls_filesz=0;
        st.tls_mods[0].tls_memsz=32; st.tls_mods[0].tls_align=8;
        st.tls_mod_count=1;
        int n0=st.tls_mod_count+2;
        st.dtv=(uintptr_t*)rtld_alloc((size_t)n0*sizeof(*st.dtv));
        for(int i=0;i<n0;i++) st.dtv[i]=0;
        st.dtv[0]=1; st.dtv[1]=mb; st.dtv_len=n0; st.dtv_store=st.dtv;
        if(st.tls_tp) *(uintptr_t*)(st.tls_tp-8)=(uintptr_t)st.dtv;
    }
    rtld_set_state(&st);
    const char *path = argc>1 ? argv[1] : "hosttlslib.so";
    void *h = rtld_dlopen(path);
    if(!h){ printf("FAIL: rtld_dlopen\n"); return 1; }
    struct rtld_lib *lib=(struct rtld_lib*)h;
    int (*tgf)(void)=(int(*)(void))rtld_dlsym(h,"tls_get_foo");
    if(!tgf){ printf("FAIL: rtld_dlsym\n"); return 1; }
    int mod=lib->tls_modid;
    if(st.dtv[mod]!=0){ printf("FAIL: DTV[%d] should start NULL\n",mod); return 1; }
    if(tgf()!=42){ printf("FAIL: tls_get_foo()=%d want 42\n",tgf()); return 1; }
    if(st.dtv[mod]==0){ printf("FAIL: DTV[%d] not filled after lazy alloc\n",mod); return 1; }
    void *foo=rtld_tls_get_addr((unsigned long)mod,0);
    if(!foo||*(int*)foo!=42){ printf("FAIL: tls_foo\n"); return 1; }
    *(int*)foo=7;
    if(*(int*)rtld_tls_get_addr((unsigned long)mod,0)!=7){ printf("FAIL: write persist\n"); return 1; }
    long *big=(long*)rtld_tls_get_addr((unsigned long)mod,4);
    for(int i=0;i<4;i++) if(big[i]!=0){ printf("FAIL: tls_big[%d] tbss\n",i); return 1; }
    printf("PASS: rtld dlopen + DTV lazy alloc + __tls_get_addr module>1 OK\n");
    return 0;
}
HEOF
} > "$work/harness.c"

gcc -I"$wt/projects/meuos-toolchain/include" \
    -I"$wt/projects/meuos-sysroot/include" \
    -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
    -o "$work/harness" "$work/harness.c"

(cd "$work" && ./harness hosttlslib.so)
echo "mt rtld dtv: PASS"
