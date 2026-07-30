#!/usr/bin/env bash
#
# scripts/daily-audit.sh — MeuOS Kit 每日代码校验与缺陷检查引擎
#
# 职责：
#   1. 实现情况校验  —— 对各子项目做构建 + make check（真实能否编译并通过测试）
#   2. 对照预期文档  —— 各子项目 ARCHITECTURE.md / 上一期 .issues 中的声明，与实际测量值对照
#   3. 缺陷检查      —— 扫描桩标记 / 未实现占位（TODO/FIXME/unimplemented/空桩…），统计密度
#   4. 对照 git 记录  —— 自上一期审计以来的提交、作者、变更文件统计
#   5. 综合评分      —— 由构建/测试结果 + 缺陷密度 + 文档一致性得出健康分与建议
#
# 输出： .issues/<MMDD>.md（四位数日期，与现有约定一致）
#
# 设计原则：可复现、零外部密钥依赖、任何一步失败只作为“发现”记录而不中断流程。
# 既能被 GitHub Actions 调用，也能本地手动运行：
#   bash scripts/daily-audit.sh                 # 用今天日期，执行构建+检查
#   bash scripts/daily-audit.sh --no-build      # 跳过构建，仅做静态/文档/git 检查
#   bash scripts/daily-audit.sh --date 0731     # 指定日期
#   bash scripts/daily-audit.sh --force         # 允许覆盖已存在的报告
#
set -uo pipefail

# ---------------------------------------------------------------------------
# 参数解析
# ---------------------------------------------------------------------------
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATE="$(date -u +%m%d)"          # 四位数 MMDD，匹配 .issues/ 约定
SYSROOT="${MEUOS_SYSROOT:-$ROOT/sysroot}"
NO_BUILD=0
FORCE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root)     ROOT="$2"; shift 2;;
    --date)     DATE="$2"; shift 2;;
    --sysroot)  SYSROOT="$2"; shift 2;;
    --no-build) NO_BUILD=1; shift;;
    --force)    FORCE=1; shift;;
    *) echo "未知参数: $1" >&2; exit 2;;
  esac
done

OUT="$ROOT/.issues/$DATE.md"
if [[ -f "$OUT" ]] && [[ $FORCE -eq 0 ]]; then
  echo "报告已存在，拒绝覆盖: $OUT （用 --force 强制）" >&2
  exit 3
fi
mkdir -p "$(dirname "$OUT")"

ISO="$(date -u +%Y-%m-%d)"
NOW="$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
HEAD="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
BRANCH="audit/$DATE"

# ---------------------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------------------
emit() { printf '%s\n' "$*" >>"$OUT"; }

# 安全计数：命令失败时返回 0，避免 set -u/pipefail 误伤
count() { local n; n=$(eval "$1" 2>/dev/null | wc -l); echo "${n:-0}"; }

# 各子项目构建顺序（sysroot 先，因其余组件依赖 MEUOS_SYSROOT）
PROJECTS=(meuos-sysroot meuos-toolchain mcc meuos-libc meow meuos-buildtools meuos-compress)

# 缺陷标记（桩 / 未实现占位）
# 注意：不含 XXX —— 本项目用 Oxxx/Jxxx/UXXX 作为 X-macro 命名约定，会误报
MARKERS=(TODO FIXME 'unimplemented' 'not implemented' 'panic("unimpl' '空桩' 'assert(0)' 'abort()' 'NYI' 'stub')

# 已知架构后端名（用于实测架构数量）
ARCH_NAMES='x86_64|aarch64|riscv64|i386|loongarch64|arm'

# ---------------------------------------------------------------------------
# 报告头
# ---------------------------------------------------------------------------
{
  echo "# 每日代码校验与缺陷检查报告 $ISO"
  echo
  echo "> 分支: \`$BRANCH\`  |  基点提交: \`$HEAD\`  |  生成时间: $NOW"
  echo
  echo "本日核查对照来源：各子项目 \`ARCHITECTURE.md\`、\`.issues/\` 历史审计、以及 \`git\` 提交记录。"
  echo "执行方式：构建+测试（实现情况）、文档声明 vs 实测（预期对照）、桩标记扫描（缺陷检查）、提交区间统计（git 对照）。"
  echo
} >"$OUT"

# ---------------------------------------------------------------------------
# 1. 组件构建与测试（实现情况校验）
# ---------------------------------------------------------------------------
emit "## 一、组件构建与测试（实现情况校验）"
emit
emit "| 组件 | 构建 | 测试 | 说明 |"
emit "|------|------|------|------|"

declare -a BUILD_STATUS TEST_STATUS COMPONENT_NOTES
idx=0
fails=0

if [[ $NO_BUILD -eq 0 ]]; then
  export MEUOS_SYSROOT="$SYSROOT"
  for p in "${PROJECTS[@]}"; do
    d="$ROOT/projects/$p"
    [[ -f "$d/Makefile" ]] || { emit "| $p | — | — | 无 Makefile，跳过 |"; BUILD_STATUS[idx]="skip"; TEST_STATUS[idx]="skip"; COMPONENT_NOTES[idx]="无 Makefile"; idx=$((idx+1)); continue; }
    # 构建
    bld="构建失败"; bin_ok=0
    if timeout 600 make -C "$d" >/tmp/audit_build_$p.log 2>&1; then
      bld="✅ PASS"; bin_ok=1
    else
      bld="❌ FAIL"; fails=$((fails+1))
    fi
    # 测试
    tst="测试跳过"; tnote=""
    if make -n -C "$d" check >/dev/null 2>&1; then
      if timeout 600 make -C "$d" check >/tmp/audit_check_$p.log 2>&1; then
        tst="✅ PASS"
      else
        tst="❌ FAIL"; fails=$((fails+1))
        tnote="$(grep -iE 'error|fail|assert' /tmp/audit_check_$p.log 2>/dev/null | tail -2 | tr '\n' ' ' | cut -c1-120)"
      fi
    fi
    emit "| $p | $bld | $tst | ${tnote:-已执行 make check} |"
    BUILD_STATUS[idx]="$bld"; TEST_STATUS[idx]="$tst"; COMPONENT_NOTES[idx]="$tnote"; idx=$((idx+1))
  done
else
  emit "| （--no-build 已指定，跳过构建与测试） | — | — | 仅做静态/文档/git 检查 |"
fi
emit

# ---------------------------------------------------------------------------
# 2. 预期文档 vs 实际代码（对照）
# ---------------------------------------------------------------------------
emit "## 二、预期文档 vs 实际代码（对照）"
emit
emit "对各子项目实测客观指标，并与 \`ARCHITECTURE.md\` / 上一期 \`.issues\` 中出现的声明数字做粗粒度对照。"
emit "若文档声明 > 实测，标记为「⚠️ 可能口径不一致」，需人工确认（非必然缺陷）。"
emit

# 上一期审计文件（排除今天）
prev="$(ls -1 "$ROOT"/.issues/[0-9][0-9][0-9][0-9].md 2>/dev/null \
        | sed 's#.*/##; s#.md##' | grep -v "^$DATE$" | sort | tail -1)"

# 从上一期 issues（及对应 ARCHITECTURE）解析关键声明数字，用于真实对照
claimed_arch=0
claimed_sysc=0
if [[ -n "$prev" ]]; then
  claimed_arch=$(grep -oE '[0-9]+[[:space:]]*架构' "$ROOT/.issues/$prev.md" 2>/dev/null \
                 | grep -oE '[0-9]+' | head -1)
  claimed_sysc=$(grep -oE '[0-9]+[[:space:]]*syscall' "$ROOT/.issues/$prev.md" 2>/dev/null \
                 | grep -oE '[0-9]+' | head -1)
fi
claimed_arch=${claimed_arch:-0}
claimed_sysc=${claimed_sysc:-0}

emit "| 组件 | .c 文件 | 代码行数 | 架构后端(实测) | 文档声明(抽样) | 一致性 |"
emit "|------|--------:|--------:|---------------:|--------------|--------|"

for p in "${PROJECTS[@]}"; do
  d="$ROOT/projects/$p"
  [[ -d "$d" ]] || continue
  cfiles=$(find "$d" -name '*.c' 2>/dev/null | wc -l)
  loc=$(find "$d" -name '*.c' -exec cat {} + 2>/dev/null | wc -l)
  # 实测架构后端：匹配已知架构名（头文件或目录）
  arch=$(grep -rhoE "($ARCH_NAMES)" "$d" --include='*.h' 2>/dev/null | sort -u | wc -l)
  # 文档声明抽样：在 ARCHITECTURE.md 与上一期 issues 中抓「数字+关键词」
  claim="—"
  if [[ -f "$d/ARCHITECTURE.md" ]]; then
    claim=$(grep -oE '[0-9]+[[:space:]]*(个|架构|后端|syscall|模块|组件|文件|行)' "$d/ARCHITECTURE.md" 2>/dev/null | head -3 | tr '\n' ';')
  fi
  if [[ -n "$prev" ]]; then
    pc=$(grep -oE "[0-9]+[[:space:]]*(个|架构|后端|syscall|模块|组件)" "$ROOT/.issues/$prev.md" 2>/dev/null | head -3 | tr '\n' ';')
    [[ -n "$pc" ]] && claim="$claim {$pc}"
  fi
  # 一致性：针对 mcc 架构数、libc syscall 数做真实对照
  cons="—"
  if [[ "$p" == "mcc" ]] && [[ $claimed_arch -gt 0 ]]; then
    if [[ $arch -lt $claimed_arch ]]; then
      cons="⚠️ 架构 实测$arch<声明$claimed_arch"
    else
      cons="✅ 架构$arch"
    fi
  elif [[ "$p" == "meuos-libc" ]] && [[ $claimed_sysc -gt 0 ]]; then
    sysc=$(find "$d" -path '*syscall*' -name '*.c' 2>/dev/null | wc -l)
    if [[ $sysc -lt $((claimed_sysc/2)) ]]; then
      cons="⚠️ syscall 实测$sysc<<声明$claimed_sysc"
    else
      cons="✅ syscall≈$sysc"
    fi
  fi
  emit "| $p | $cfiles | $loc | $arch | ${claim:-—} | $cons |"
done
emit
emit "（架构后端为「已知架构名在头文件中出现去重计数」，用于快速发现后端被删减；并非精确清单。）"
emit

# ---------------------------------------------------------------------------
# 3. 缺陷检查（桩标记扫描）
# ---------------------------------------------------------------------------
emit "## 三、缺陷检查（桩 / 未实现占位扫描）"
emit
total_defects=0
emit "| 标记 | 命中数 | 示例位置 |"
emit "|------|------:|----------|"
for m in "${MARKERS[@]}"; do
  # 固定字符串匹配（避免括号被当作正则分组）；排除构建产物目录
  hits=$(grep -rniF "$m" "$ROOT/projects" --include='*.c' --include='*.h' \
         --exclude-dir=build 2>/dev/null | wc -l)
  total_defects=$((total_defects + hits))
  sample=""
  if [[ $hits -gt 0 ]]; then
    sample=$(grep -rniF "$m" "$ROOT/projects" --include='*.c' --include='*.h' \
             --exclude-dir=build 2>/dev/null | head -3 \
             | sed 's#'"$ROOT"'/##' | cut -c1-80 | tr '\n' ' ')
  fi
  emit "| \`$m\` | $hits | $sample |"
done
emit

# 缺陷密度（每千行 .c 代码）
all_loc=$(find "$ROOT/projects" -name '*.c' 2>/dev/null -exec cat {} + | wc -l)
if [[ $all_loc -gt 0 ]]; then
  density=$(awk "BEGIN{printf \"%.2f\", $total_defects / $all_loc * 1000}")
else
  density=0
fi
emit "桩标记合计 **$total_defects** 处；覆盖代码 **$all_loc** 行；密度 **$density** 处/千行。"
emit

# 可选：若环境装有 cppcheck，补充静态分析错误数
if command -v cppcheck >/dev/null 2>&1; then
  cpp_err=$(cppcheck --quiet --enable=warning,style "$ROOT/projects" 2>&1 | grep -cE '\[' || true)
  emit "cppcheck（warning+style）报错约 **${cpp_err:-0}** 处（环境可选，未安装则跳过）。"
fi
emit

# ---------------------------------------------------------------------------
# 4. 对照 git 提交记录
# ---------------------------------------------------------------------------
emit "## 四、对照 git 提交记录（自上一期审计以来）"
emit
if [[ -n "$prev" ]]; then
  prev_commit=$(git -C "$ROOT" log -1 --format=%H -- ".issues/$prev.md" 2>/dev/null)
  since_iso=$(git -C "$ROOT" show -s --format=%ci "$prev_commit" 2>/dev/null)
  emit "上一期审计：\`.issues/$prev.md\`（提交 \`${prev_commit:0:8}\`, $since_iso）"
  range="$prev_commit..HEAD"
else
  since_iso="1 day ago"
  emit "未发现历史审计文件，按「近 1 天」统计。"
  range="HEAD"
fi
emit
emit "### 提交列表"
emit
commits=$(git -C "$ROOT" log --since="$since_iso" --oneline 2>/dev/null)
if [[ -z "$commits" ]]; then
  emit "_（该区间无新提交）_"
else
  emit '```text'
  echo "$commits" >>"$OUT"
  emit '```'
fi
emit
emit "### 作者贡献"
emit
shortlog=$(git -C "$ROOT" shortlog -sn --since="$since_iso" 2>/dev/null)
if [[ -n "$shortlog" ]]; then
  emit '```text'
  echo "$shortlog" >>"$OUT"
  emit '```'
else
  emit "_（无）_"
fi
emit
changed=$(git -C "$ROOT" diff --name-only "$range" -- projects/ 2>/dev/null | wc -l)
emit "变更文件（projects/）:**$changed** 个。"
emit

# ---------------------------------------------------------------------------
# 5. 综合评分与建议
# ---------------------------------------------------------------------------
emit "## 五、综合评分与建议"
emit
# 评分：基础 100，构建/测试失败扣分，缺陷密度高扣分
score=100
[[ $fails -gt 0 ]] && score=$((score - fails * 12))
if awk "BEGIN{exit !($density > 5)}"; then score=$((score - 15)); fi
if awk "BEGIN{exit !($density > 10)}"; then score=$((score - 15)); fi
[[ $score -lt 0 ]] && score=0

verdict="需关注"
if   [[ $score -ge 90 ]] && [[ $fails -eq 0 ]]; then verdict="优秀"
elif [[ $score -ge 75 ]] && [[ $fails -eq 0 ]]; then verdict="良好"
elif [[ $score -ge 50 ]]; then verdict="需关注"
else verdict="阻塞"; fi

emit "**健康分：$score / 100　结论：$verdict**"
emit
emit "评分口径：基础 100；每出现一次构建/测试失败 −12；桩密度 >5/千行 −15，>10/千行再 −15。"
emit
emit "### 行动建议"
emit
any=0
if [[ $fails -gt 0 ]]; then
  any=1
  emit "- 🛠 存在 $fails 个构建/测试失败组件，优先修复（见第一节表格，详情见 CI 日志与 \`/tmp/audit_*.log\`）。"
fi
if awk "BEGIN{exit !($density > 5)}"; then
  any=1
  emit "- 🔍 桩标记密度偏高（$density/千行），建议清理或登记为已知 TODO（见第三节）。"
fi
if [[ -n "$prev" ]] && [[ $changed -gt 0 ]]; then
  any=1
  emit "- 📝 自上一期（\`$prev\`）以来 projects/ 变更 $changed 个文件，建议核对是否与本期缺陷/文档声明相关。"
fi
if [[ $any -eq 0 ]]; then
  emit "- 本期未见阻断性异常，可保持现状并继续按计划推进。"
fi
emit
emit "---"
emit "_本报告由 \`scripts/daily-audit.sh\` 自动生成，属数据驱动的初步筛查，深度语义审查仍建议由人工/Agent 复核。_"
emit

echo "✅ 已生成报告: $OUT"
exit 0
