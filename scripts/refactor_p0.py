#!/usr/bin/env python3
"""P0 refactoring: unify version/program_name across utils tools."""
import re, os, sys

base = 'projects/meuos-utils/src/utils/'

# All tool files that need refactoring (relative to base)
tools = []
for root, dirs, files in os.walk(base):
    for f in sorted(files):
        if not f.endswith('.c'):
            continue
        path = os.path.join(root, f)
        relpath = os.path.relpath(path, base)
        tools.append(relpath)

for relpath in sorted(tools):
    path = os.path.join(base, relpath)
    with open(path) as fh:
        src = fh.read()

    # Skip files already using utils.h (net/ already done)
    if '#include "meuos/utils.h"' in src:
        print(f'  SKIP (already has utils.h): {relpath}')
        continue

    # Skip files that don't have the version string pattern
    if 'static const char' not in src or 'meuos-utils' not in src:
        # Check for version_str variant (uname uses version_str)
        if 'static const char version_str[]' in src:
            pass  # handle below
        else:
            print(f'  SKIP (no version string): {relpath}')
            continue

    orig = src

    # 1. Add include after last system #include <...> line
    lines = src.split('\n')
    last_inc = -1
    for i, line in enumerate(lines):
        if line.startswith('#include <'):
            last_inc = i
    if last_inc >= 0:
        lines.insert(last_inc + 1, '')
        lines.insert(last_inc + 2, '#include "meuos/utils.h"')
        src = '\n'.join(lines)

    # 2. Remove static const char version[] = "...meuos-utils..."; line
    src = re.sub(
        r'\nstatic const char \w*\[\] = "0\.1\.0-[^\"]+ \(meuos-utils\)";\n',
        '\n', src)

    # 3. Replace manual --version check with utils_init
    # Pattern: if (argc > 1 && !strcmp(argv[1], "--version")) { ... return 0; }
    pat_ver = r'if \(argc > 1 && !strcmp\(argv\[1\], "--version"\)\) \{[^}]+\}\n?\s*'
    src = re.sub(pat_ver, 'int argi = utils_init(argc, argv);\n    ', src, count=1)

    # 4. Update --help check from argv[1] to argv[argi]
    src = src.replace(
        'if (argc > 1 && !strcmp(argv[1], "--help"))',
        'if (argi < argc && !strcmp(argv[argi], "--help"))')

    # 5. Remove duplicate 'int argi = 1;' after utils_init
    src = src.replace(
        'int argi = utils_init(argc, argv);\n    int argi = 1;',
        'int argi = utils_init(argc, argv);')

    # 6. Also handle 'int argi = 1;' that appears later (in option parsing)
    # Only remove if it's a standalone line right after the utils_init block
    # Actually, keep argi = 1 if it wasn't replaced - some tools define argi later

    if src != orig:
        with open(path, 'w') as fh:
            fh.write(src)
        print(f'  DONE: {relpath}')
    else:
        print(f'  NOCHANGE: {relpath}')

print('\nAll done.')
