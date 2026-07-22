#!/bin/sh
# test/ar_bsd_format.sh - BSD #1/ extended-name archive format test.
# Creates a BSD-format archive (with #1/ long names) and verifies
# mt/ar can list, extract, and that mt/ld can link with it.
set -eu
as=${1:?as path required}
ar=${2:?ar path required}
ld=${3:?ld path required}
work=$(mktemp -d /tmp/mt-bsd-test.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Create an object with a long filename
echo 'int bsd_long_name_function(void) { return 77; }' > "$work/this_is_a_very_long_member_name.c"
cc -c "$work/this_is_a_very_long_member_name.c" -o "$work/this_is_a_very_long_member_name.o"

# Build a BSD #1/ format archive using Python (host ar may not support BSD)
python3 - "$work" << 'PY'
import sys, struct
work = sys.argv[1]
with open(f"{work}/this_is_a_very_long_member_name.o", "rb") as f:
    obj = f.read()
name = "this_is_a_very_long_member_name.o"
nlen = len(name)
data = name.encode() + obj
hdr = f"#1/{nlen}".ljust(16)
hdr += "0".ljust(12) + "0".ljust(6) + "0".ljust(6) + "100644".ljust(8)
hdr += str(len(data)).ljust(10) + "`\n"
with open(f"{work}/libbsd.a", "wb") as a:
    a.write(b"!<arch>\n")
    a.write(hdr.encode("ascii"))
    a.write(data)
    if len(data) % 2: a.write(b"\n")
PY

# 1. List
"$ar" t "$work/libbsd.a" | grep -q "this_is_a_very_long_member_name.o" || {
    echo "mt ar BSD format: FAIL (list)"; exit 1; }

# 2. Extract + compare
mkdir -p "$work/extract"
(cd "$work/extract" && "$ar" x "$work/libbsd.a")
cmp "$work/this_is_a_very_long_member_name.o" "$work/extract/this_is_a_very_long_member_name.o" || {
    echo "mt ar BSD format: FAIL (extract)"; exit 1; }

# 3. Link with mt/ld
echo 'extern int bsd_long_name_function(void); int main(void){return bsd_long_name_function()==77?0:1;}' > "$work/main.c"
cc -c "$work/main.c" -o "$work/main.o"
printf '.text\n.globl _start\n_start:\n\tcallq main\n\tmovl %%eax,%%edi\n\tmovl $60,%%eax\n\tsyscall\n' > "$work/start.s"
"$as" -o "$work/start.o" "$work/start.s"
"$ld" -o "$work/app" "$work/start.o" "$work/main.o" "$work/libbsd.a" || {
    echo "mt ar BSD format: FAIL (link)"; exit 1; }
"$work/app" || { echo "mt ar BSD format: FAIL (run)"; exit 1; }

echo "mt ar BSD format: PASS"
