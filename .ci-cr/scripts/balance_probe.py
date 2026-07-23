#!/usr/bin/env python3
# balance_probe.py - 多模型余额探针
#
# 读取 config/model_router.yaml，探测各 provider 余额，写入
# state/balance_snapshots/latest.json。
#
# 门控策略（SPEC §4.1）：
#   - 主力余额 > 20%：承担规划与审查
#   - 5% < 余额 < 20%：仅承担审查（规划转交 fallback）
#   - 余额 < 5%：大脑休眠
#   - 查询超时：保守假设余额充足，但强制降级为单任务并发
#
# 用法：python3 balance_probe.py [--config model_router.yaml] [--out latest.json]

import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CICR_ROOT = os.path.dirname(HERE)

try:
    import yaml
except ImportError:
    print("PyYAML not available; using built-in fallback parser", file=sys.stderr)
    yaml = None


def load_yaml(path):
    if yaml is not None:
        with open(path) as f:
            return yaml.safe_load(f)
    # 极简回退：不依赖 PyYAML 时只读 thresholds
    cfg = {"models": {}, "thresholds": {}}
    section = None
    with open(path) as f:
        for line in f:
            s = line.rstrip()
            if not s or s.lstrip().startswith("#"):
                continue
            if not s.startswith(" ") and s.endswith(":"):
                section = s[:-1]
                cfg[section] = cfg.get(section, {})
            elif section and ":" in s:
                k, _, v = s.strip().partition(":")
                v = v.strip().split("#", 1)[0].strip()
                if v:
                    cfg[section][k] = v
    return cfg


def run(cmd, timeout=20):
    """运行命令，返回 (rc, stdout, stderr)。超时返回 rc=124。"""
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"
    except FileNotFoundError:
        return 127, "", "command not found: " + cmd[0]


def probe_codebuddy(model):
    """CodeBuddy 探活：用 --version + 一次 dry-run -p 'ping' 探测 auth 与积分可用性。
    积分耗尽也会返回 0，但无 chat 调用=不浪费信用。
    成功 -> 1.0；auth 失败 -> 0.0；超时 -> 0.5；429/quota -> 0.0。"""
    # 先 --version 快速验证 CLI 可执行（~1s）
    rc, out, err = run(["codebuddy", "--version"], timeout=5)
    if rc == 127:
        return 0.0, "no_cli", "codebuddy not found on PATH"
    if rc == 124:
        return 0.5, "timeout", "codebuddy --version timeout"
    # 鉴权 + 模型可用性：发最小 prompt（5s 应有结果）
    cmd = ["codebuddy", "-p", "ok", "--output-format", "json",
           "--model", model, "--max-turns", "1", "--permission-mode", "plan"]
    rc, out, err = run(cmd, timeout=12)
    combined = out + err
    if rc == 0 and "Authentication required" not in combined:
        return 1.0, "ok", None
    if rc == 124:
        return 0.5, "timeout", "probe timed out; assume sufficient (degraded)"
    if "Authentication required" in combined or "login" in combined.lower():
        return 0.0, "unauth", "codebuddy not logged in"
    if "429" in combined or "quota" in combined.lower() or "exhausted" in combined.lower():
        return 0.0, "exhausted", combined[-200:]
    return 0.5, "unknown", combined[-200:]


def probe_arkcli(product):
    """arkcli usage balance --type plan --product <product> --format json。
    解析 weekly 与 session 周期剩余配额比例（percent=已用%，100-=剩余）。
    默认采用 weekly；与 minimax 同语义。"""
    cmd = ["arkcli", "usage", "balance", "--type", "plan",
           "--product", product, "--format", "json"]
    rc, out, err = run(cmd, timeout=25)
    if rc != 0:
        return 1.0, "probe_failed", (err or out)[-200:]
    try:
        data = json.loads(out)
    except json.JSONDecodeError:
        return 1.0, "parse_failed", "non-JSON output"
    # arkcli 输出格式：{items:[{periods:[{label,percent,reset_at},...]}]}
    # percent 是"已使用"百分比（100=用完，0=全空）。
    used_pct = None
    label_pick = "weekly"
    if isinstance(data, dict):
        items = data.get("items") or []
        for it in items:
            for p in it.get("periods") or []:
                if p.get("label") == label_pick:
                    used_pct = float(p.get("percent", 0))
                    break
            if used_pct is not None:
                break
    if used_pct is None:
        # 老格式兼容
        for key in ("remaining_ratio", "balance_ratio", "remain_ratio"):
            if key in data:
                return max(0.0, min(1.0, float(data[key]))), "ok", None
        remain = data.get("remaining") or data.get("remain")
        total = data.get("total") or data.get("quota")
        if remain is not None and total:
            return max(0.0, min(1.0, float(remain) / float(total))), "ok", None
        return 1.0, "ok_no_ratio", "arkcli balance OK, ratio indeterminate"
    ratio = max(0.0, min(1.0, 1.0 - used_pct / 100.0))
    return ratio, "ok", json.dumps({"used_pct": used_pct, "label": label_pick})


def probe_codex(model):
    """Codex 无 user/info 端点。先用 codex doctor 检查健康，再用最小 exec 验证可用性。
    成功 -> 1.0；失败 -> 0.0。"""
    rc, out, err = run(["codex", "doctor"], timeout=20)
    if rc != 0:
        return 0.0, "unavailable", (err or out)[-200:]
    # doctor 通过后再做最小 exec 验证（确认可实际调用）
    cmd = ["codex", "exec", "--json", "reply OK"]
    rc2, out2, err2 = run(cmd, timeout=30)
    if rc2 == 0 and "thread.started" in (out2 or ""):
        return 1.0, "ok", None
    if "auth" in (err2 or out2 or "").lower() or "login" in (err2 or out2 or "").lower():
        return 0.0, "unauth", "codex not authenticated"
    # exec 失败但 doctor 通过：保守假设可用（可能是临时网络问题）
    return 0.5, "degraded", (err2 or out2 or "")[-200:]


def probe_nvidia_nim(model, base_url="https://integrate.api.nvidia.com/v1"):
    """NIM free tier 无余额概念。仅 ping /v1/models 端点验证 auth + 可达性。
    成功 -> 1.0；429 -> 0.3（限流降级）；401 -> 0.0；超时 -> 0.5。
    不发实际 chat 请求以节省 token + 避免排队。"""
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from providers import _load_nim_api_key
        api_key, _ = _load_nim_api_key()
        if not api_key:
            return 0.0, "no_api_key", "set NVIDIA_API_KEY or config/secret.yaml"
    except Exception as e:
        return 0.0, "import_fail", str(e)[:200]

    # 仅检查 /v1/models 端点（不消耗 token）
    import urllib.request
    import urllib.error
    url = (base_url or "https://integrate.api.nvidia.com/v1").rstrip("/") + "/models"
    req = urllib.request.Request(url, method="GET",
        headers={"Authorization": "Bearer " + api_key, "Accept": "application/json"})
    ctx = None
    if os.environ.get("CICR_TLS_INSECURE") in ("1", "true", "yes"):
        import ssl
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
    opener_args = {"timeout": 8}
    if ctx is not None:
        opener_args["context"] = ctx
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, **opener_args) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            dt = time.time() - t0
        # models 端点含目标 model 即视为可用
        if model in body or '"data"' in body:
            return 1.0, f"models_ok ({round(dt,1)}s)", None
        return 0.5, f"models_no_match ({round(dt,1)}s)", body[:200]
    except urllib.error.HTTPError as e:
        dt = time.time() - t0
        try: body = e.read().decode()[:200]
        except: body = ""
        if e.code == 429:
            return 0.3, "rate_limited", body
        if e.code in (401, 403):
            return 0.0, "unauth", body
        return 0.5, f"http_{e.code} ({round(dt,1)}s)", body
    except Exception as e:
        dt = time.time() - t0
        return 0.5, f"timeout/err ({round(dt,1)}s)", str(e)[:200]


def probe_minimax(model, base_url="https://api.minimaxi.com/v1"):
    """MiniMax 探活：仅查 /v1/token_plan/remains（不消耗 token）。
    该端点 200 即视为可用；weekly 余额比例由 probe_minimax_balance 直接返回。"""
    # 复用 probe_minimax_balance；chat 探活已移除（节省 token + 避免 529 误判）
    return probe_minimax_balance(base_url)


def probe_minimax_balance(base_url="https://api.minimaxi.com"):
    """查询 minimax /v1/token_plan/remains 真实额度。
    返回 (ratio, status, info)。ratio = weekly 剩余比例（百分比转 0-1）。
    当 weekly<10% 时返回极低值，驱动会自动降级。"""
    import urllib.request
    import urllib.error
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from providers import _load_provider_key
        api_key, _ = _load_provider_key("minimax")
        if not api_key:
            return 0.5, "no_api_key", "set $MINIMAX_API_KEY"
    except Exception as e:
        return 0.5, "import_fail", str(e)[:200]
    url = (base_url or "https://api.minimaxi.com").rstrip("/") + "/v1/token_plan/remains"
    req = urllib.request.Request(url, method="GET",
        headers={"Authorization": "Bearer " + api_key, "Accept": "application/json"})
    ctx = None
    if os.environ.get("CICR_TLS_INSECURE") in ("1", "true", "yes"):
        import ssl
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
    opener_args = {"timeout": 12}
    if ctx is not None:
        opener_args["context"] = ctx
    try:
        with urllib.request.urlopen(req, **opener_args) as resp:
            body = resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        return 0.0, f"http_{e.code}", body[:200]
    except Exception as e:
        return 0.5, "net_err", str(e)[:200]
    try:
        data = json.loads(body)
    except json.JSONDecodeError:
        return 0.5, "parse_err", body[:200]
    remains = data.get("model_remains") or []
    weekly_pct = None
    interval_pct = None
    for r in remains:
        if r.get("model_name") in ("general", "M3", "minimax-m3"):
            weekly_pct = r.get("current_weekly_remaining_percent")
            interval_pct = r.get("current_interval_remaining_percent")
            break
    if weekly_pct is None and remains:
        # 取第一条
        r = remains[0]
        weekly_pct = r.get("current_weekly_remaining_percent")
        interval_pct = r.get("current_interval_remaining_percent")
    if weekly_pct is None:
        return 0.5, "no_data", json.dumps(remains)[:200]
    ratio = max(0.0, min(1.0, float(weekly_pct) / 100.0))
    info = {
        "weekly_pct": weekly_pct,
        "interval_pct": interval_pct,
        "model_count": len(remains),
    }
    if ratio >= 0.5:
        return ratio, "ok_weekly", json.dumps(info)
    elif ratio >= 0.1:
        return ratio, "low_weekly", json.dumps(info)
    else:
        return ratio, "critical_weekly", json.dumps(info)


def probe_one(name, spec):
    provider = spec.get("provider", "")
    if provider == "codebuddy":
        ratio, status, err = probe_codebuddy(spec.get("model", "default-model"))
    elif provider == "arkcli":
        ratio, status, err = probe_arkcli(spec.get("plan", "coding-plan"))
    elif provider == "codex":
        ratio, status, err = probe_codex(spec.get("model", ""))
    elif provider == "nvidia_nim":
        ratio, status, err = probe_nvidia_nim(
            spec.get("model", "z-ai/glm-5.2"),
            spec.get("base_url", "https://integrate.api.nvidia.com/v1"),
        )
    elif provider == "minimax":
        # 优先级：先查 /v1/token_plan/remains（无消耗），再决定是否要小 chat 探活
        ratio, status, err = probe_minimax_balance(
            spec.get("token_plan_url") or spec.get("base_url") or
            "https://www.minimaxi.com",
        )
    else:
        ratio, status, err = 0.0, "unknown_provider", provider
    return {
        "provider": provider,
        "model": spec.get("model") or spec.get("plan"),
        "base_url": spec.get("base_url"),
        "balance_ratio": round(ratio, 4),
        "status": status,
        "error": err,
    }


def select_brain(snapshots, thresholds):
    """按 SPEC §4.1 门控策略选择本轮大脑。
    返回 (selection_name | None, available, degraded)。
    优先顺序：brain_primary -> brain_fallback -> brain_emergency。
    每个角色按 balance_ratio vs critical_balance 判定。
    当所有角色都 < critical 时，退到"任一可用 provider"（ratio>0.3）。"""
    critical = float(thresholds.get("critical_balance", 0.05))
    min_ok = float(thresholds.get("min_balance_ratio", 0.2))
    order = ["brain_primary", "brain_fallback", "brain_emergency"]
    for name in order:
        snap = snapshots.get(name)
        if not snap:
            continue
        ratio = snap["balance_ratio"]
        if ratio > critical:
            return name, True, ratio < min_ok
    # 所有 brain_* 都低：尝试"任一可用"（除 executor）
    any_ok = []
    for name, snap in snapshots.items():
        if not name.startswith("brain"):
            continue
        if snap["balance_ratio"] >= 0.3:
            any_ok.append((snap["balance_ratio"], name))
    if any_ok:
        any_ok.sort(reverse=True)
        name = any_ok[0][1]
        return name, True, True  # degraded=True，标记低额度
    return None, False, False


def main():
    ap = argparse.ArgumentParser(description="MeuOS Kit CI/CR 余额探针")
    ap.add_argument("--config", default=os.path.join(CICR_ROOT, "config", "model_router.yaml"))
    ap.add_argument("--out", default=os.path.join(CICR_ROOT, "state", "balance_snapshots", "latest.json"))
    args = ap.parse_args()

    cfg = load_yaml(args.config)
    models = cfg.get("models", {})
    thresholds = cfg.get("thresholds", {})

    snapshots = {}
    for name, spec in models.items():
        t0 = time.time()
        snapshots[name] = probe_one(name, spec)
        snapshots[name]["probe_ms"] = int((time.time() - t0) * 1000)

    selection, available, degraded = select_brain(snapshots, thresholds)
    result = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "models": snapshots,
        "brain_selection": selection,
        "brain_available": available,
        "degraded_single_task": degraded or not available,
        "critical_balance": float(thresholds.get("critical_balance", 0.05)),
    }

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(result, f, indent=2, ensure_ascii=False)
    print(json.dumps(result, indent=2, ensure_ascii=False))

    # 退出码：大脑不可用时返回 2（驱动据此休眠规划，但仍执行已有 Hy3 任务）
    return 0 if available else 2


if __name__ == "__main__":
    sys.exit(main())
