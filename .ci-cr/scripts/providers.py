#!/usr/bin/env python3
# providers.py - 统一模型 provider 调度（codebuddy / codex）
#
# 读取 config/model_router.yaml，按角色（role）解析 provider 与 model，
# 并提供统一的调用接口。task_executor.py 与 milestone_reviewer.py 共用此模块。
#
# 支持的 provider：
#   - codebuddy：codebuddy -p ... --output-format json --model ...
#                会话恢复：codebuddy -r <session_id> -p ...
#   - codex：    codex exec --json -m <model> -o <file> -s workspace-write <prompt>
#                会话恢复：codex exec resume <thread_id> --json ... <prompt>
#   - nvidia_nim：HTTP REST，OpenAI 兼容 /v1/chat/completions（NIM free tier）
#                API key 优先级：env $NVIDIA_API_KEY > config/secret.yaml
#                不支持会话恢复（free tier 无 thread/state）
#   - minimax：MiniMax API v2 /v1/text/chatcompletion_v2
#                API key：env $MINIMAX_API_KEY > secret.yaml
#                走 thinking:disabled 模式获取纯 content（避免 token 在 reasoning 耗尽）
#
# 用法：
#   from providers import load_model_router, get_role, run_model, ProviderResult

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CICR_ROOT = os.path.dirname(HERE)
CONFIG_PATH = os.path.join(CICR_ROOT, "config", "model_router.yaml")

try:
    import yaml
except ImportError:
    yaml = None


def _log(msg):
    print(f"[provider] {msg}", file=sys.stderr)


def load_model_router():
    """读取 model_router.yaml，返回 (models_dict, thresholds_dict)。"""
    if not os.path.exists(CONFIG_PATH):
        return {}, {}
    if yaml:
        with open(CONFIG_PATH) as f:
            cfg = yaml.safe_load(f) or {}
        return cfg.get("models", {}) or {}, cfg.get("thresholds", {}) or {}
    # 无 PyYAML 时的回退
    models, thresholds = {}, {}
    section = None
    cur = None
    with open(CONFIG_PATH) as f:
        for line in f:
            s = line.rstrip()
            if not s or s.lstrip().startswith("#"):
                continue
            if not s.startswith(" ") and s.endswith(":"):
                section = s[:-1]
                if section in ("models", "thresholds"):
                    cfg = models if section == "models" else thresholds
            elif section and ":" in s:
                k, _, v = s.strip().partition(":")
                v = v.strip().split("#", 1)[0].strip()
                if v and section in ("models", "thresholds"):
                    cfg[k] = v
    return models, thresholds


def get_role(role_name):
    """返回某角色的配置 dict：{provider, model/plan, min_balance_ratio}。
    若角色不存在返回 None。"""
    models, _ = load_model_router()
    return models.get(role_name)


# --------------------------------------------------------------------------- #
# CodeBuddy 调用
# --------------------------------------------------------------------------- #

def run_codebuddy(prompt, model="default-model", resume_sid=None,
                  max_turns=40, permission_mode="bypassPermissions",
                  allowed_tools=None, disallowed_tools=None,
                  system_prompt=None, timeout=900, **kwargs):
    """调用 codebuddy --print --output-format json。
    返回 ProviderResult(rc, session_id, text, raw)。
    kwargs 接受任何 codebuddy 之外的杂项（如 _nim_per_call_timeout）但忽略。"""
    cmd = ["codebuddy", "-p", prompt, "--output-format", "json",
           "--model", str(model), "--max-turns", str(max_turns),
           "--permission-mode", permission_mode]
    if allowed_tools:
        cmd += ["--allowedTools"] + list(allowed_tools)
    if disallowed_tools:
        cmd += ["--disallowedTools"] + list(disallowed_tools)
    if system_prompt:
        cmd += ["--system-prompt", system_prompt]
    if resume_sid:
        cmd += ["-r", resume_sid]

    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return ProviderResult(rc=124, session_id=resume_sid, text="", raw="")
    except FileNotFoundError:
        return ProviderResult(rc=127, session_id=resume_sid, text="", raw="")

    out = p.stdout
    sid = resume_sid
    text = out
    try:
        data = json.loads(out)
        if isinstance(data, dict):
            sid = data.get("session_id") or sid
            text = data.get("result", "")
            if data.get("subtype") == "error":
                text = data.get("error", text)
    except (json.JSONDecodeError, TypeError):
        pass
    return ProviderResult(rc=p.returncode, session_id=sid, text=text, raw=out)


# --------------------------------------------------------------------------- #
# Codex 调用
# --------------------------------------------------------------------------- #

def run_codex(prompt, model=None, resume_sid=None,
              sandbox="workspace-write", timeout=900, _retried=False,
              **kwargs):
    """调用 codex exec --json -m <model> -o <tmpfile> <prompt>。
    返回 ProviderResult(rc, session_id, text, raw)。
    session_id = codex 的 thread_id（从 thread.started 事件提取）。

    容错：若指定了 model 但调用失败（model 名 codex 不识别），自动回退到
    codex 默认 model 重试一次。

    kwargs 接受 codebuddy 专属参数（permission_mode/max_turns/allowed_tools 等），
    将 permission_mode 映射到 codex sandbox 级别，其余忽略。"""
    # 将 codebuddy permission_mode 映射到 codex sandbox
    pm = kwargs.get("permission_mode")
    if pm == "plan":
        sandbox = "read-only"
    elif pm in ("bypassPermissions", "dontAsk", "auto"):
        sandbox = "danger-full-access"
    elif pm == "acceptEdits":
        sandbox = "workspace-write"

    last_msg = tempfile.NamedTemporaryFile(mode="w+", suffix=".txt", delete=False)
    last_msg.close()
    last_msg_path = last_msg.name

    if resume_sid:
        cmd = ["codex", "exec", "resume", str(resume_sid), "--json",
               "-o", last_msg_path, "-s", sandbox]
        if model:
            cmd += ["-m", str(model)]
        cmd.append(prompt)
    else:
        cmd = ["codex", "exec", "--json", "-o", last_msg_path, "-s", sandbox]
        if model:
            cmd += ["-m", str(model)]
        cmd.append(prompt)

    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        _clean(last_msg_path)
        return ProviderResult(rc=124, session_id=resume_sid, text="", raw="")
    except FileNotFoundError:
        _clean(last_msg_path)
        return ProviderResult(rc=127, session_id=resume_sid, text="", raw="")

    # 从 JSONL 提取 thread_id 和最终消息
    sid = resume_sid
    text = ""
    raw_lines = p.stdout.splitlines() if p.stdout else []
    for line in raw_lines:
        line = line.strip()
        if not line:
            continue
        try:
            evt = json.loads(line)
        except json.JSONDecodeError:
            continue
        if evt.get("type") == "thread.started" and "thread_id" in evt:
            sid = evt["thread_id"]
        elif evt.get("type") == "item.completed":
            item = evt.get("item", {})
            if item.get("type") == "agent_message":
                text = item.get("text", text)

    # -o 文件作为最终消息的权威来源
    try:
        with open(last_msg_path) as f:
            file_text = f.read().strip()
        if file_text:
            text = file_text
    except (OSError, IOError):
        pass
    _clean(last_msg_path)

    # 容错：指定了 model 但调用失败（rc!=0 且无文本）-> 回退到默认 model 重试
    if model and p.returncode != 0 and not text and not _retried:
        _log(f"codex rc={p.returncode} with model={model!r}; retrying with default model")
        return run_codex(prompt, model=None, resume_sid=resume_sid,
                         sandbox=sandbox, timeout=timeout, _retried=True)

    return ProviderResult(rc=p.returncode, session_id=sid, text=text,
                          raw=p.stdout)


def _clean(path):
    try:
        os.remove(path)
    except OSError:
        pass


# --------------------------------------------------------------------------- #
# NVIDIA NIM 调用（HTTP REST，OpenAI 兼容）
# --------------------------------------------------------------------------- #

def _load_nim_api_key():
    """NIM API key 加载顺序：env > config/secret.yaml > None。"""
    key = os.environ.get("NVIDIA_API_KEY")
    if key:
        return key, "env"
    secret_path = os.path.join(CICR_ROOT, "config", "secret.yaml")
    if os.path.exists(secret_path):
        try:
            import yaml as _y
            with open(secret_path) as f:
                doc = _y.safe_load(f) or {}
            key = (doc.get("nvidia_nim") or {}).get("api_key")
            if key:
                return key, "secret.yaml"
        except Exception:
            pass
    return None, None


def _load_provider_key(provider_name):
    """通用 provider API key 加载。env > secret.yaml.providers.<name>."""
    env_var = {
        "nvidia_nim": "NVIDIA_API_KEY",
        "minimax": "MINIMAX_API_KEY",
        "arkcli": "ARK_API_KEY",
    }.get(provider_name)
    if env_var:
        key = os.environ.get(env_var)
        if key:
            return key, "env"
    secret_path = os.path.join(CICR_ROOT, "config", "secret.yaml")
    if os.path.exists(secret_path):
        try:
            import yaml as _y
            with open(secret_path) as f:
                doc = _y.safe_load(f) or {}
            key = (doc.get("providers") or {}).get(provider_name)
            if isinstance(key, dict):
                key = key.get("api_key")
            if key:
                return key, "secret.yaml"
        except Exception:
            pass
    return None, None


def _nim_post_chat(base_url, api_key, model, messages,
                   max_tokens=2048, temperature=0.2, timeout=120):
    """调用 NIM /v1/chat/completions。返回 (status, body_text, err_text)。
    使用 urllib（无第三方依赖）。
    应急逃生口：通过 env CICR_TLS_INSECURE=1 跳过 TLS 校验（自签/失效证书反代；
    默认 HTTPS 证书有效，不要在生产启用）。"""
    import urllib.request
    import urllib.error
    url = (base_url or "https://integrate.api.nvidia.com/v1").rstrip("/") \
        + "/chat/completions"
    payload = json.dumps({
        "model": model,
        "messages": messages,
        "max_tokens": int(max_tokens),
        "temperature": float(temperature),
        "stream": False,
    }).encode("utf-8")
    req = urllib.request.Request(
        url, data=payload, method="POST",
        headers={
            "Authorization": "Bearer " + api_key,
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
    )
    ctx = None
    if os.environ.get("CICR_TLS_INSECURE") in ("1", "true", "yes"):
        import ssl
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
    opener_args = {"timeout": timeout}
    if ctx is not None:
        opener_args["context"] = ctx
    try:
        with urllib.request.urlopen(req, **opener_args) as resp:
            return resp.status, resp.read().decode("utf-8", errors="replace"), ""
    except urllib.error.HTTPError as e:
        try:
            body = e.read().decode("utf-8", errors="replace")
        except Exception:
            body = str(e)
        return e.code, body, str(e)
    except urllib.error.URLError as e:
        return 0, "", "URLError: " + str(e.reason)
    except Exception as e:
        return 0, "", "Exception: " + type(e).__name__ + ": " + str(e)


def run_nvidia_nim(prompt, model="z-ai/glm-5.2",
                   base_url="https://integrate.api.nvidia.com/v1",
                   resume_sid=None, timeout=120, **kwargs):
    """调用 NIM /v1/chat/completions。返回 ProviderResult。
    session_id：NIM 不持久会话，保留 prompt 摘要做去重键。
    kwargs 支持：max_tokens、temperature、system_prompt。
    401/403/网络错时返回 rc!=0；429 时返回 rc=124（调用层会触发退避）。
    """
    api_key, key_src = _load_nim_api_key()
    if not api_key:
        return ProviderResult(
            rc=127, session_id=resume_sid,
            text="NIM api_key not found (set $NVIDIA_API_KEY or config/secret.yaml)",
            raw="",
        )

    system_prompt = kwargs.get("system_prompt")
    messages = []
    if system_prompt:
        messages.append({"role": "system", "content": system_prompt})
    messages.append({"role": "user", "content": prompt})

    max_tokens = int(kwargs.get("max_tokens", 4096))
    temperature = float(kwargs.get("temperature", 0.2))

    # 模型回退链：主 model + kwargs 中的 fallback_models 列表
    candidates = [model]
    fb = kwargs.get("fallback_models")
    if isinstance(fb, (list, tuple)):
        for m in fb:
            if isinstance(m, str) and m and m not in candidates:
                candidates.append(m)

    # 单次调用超时（秒）：可在 model_router.yaml 的 thresholds.nim_per_call_timeout 配置
    per_timeout = timeout
    rt = kwargs.get("_nim_per_call_timeout")
    if isinstance(rt, (int, float)) and rt > 0:
        per_timeout = int(rt)

    # 串行 or 并发回退
    concurrent = kwargs.get("_nim_concurrent_fallback", True) and len(candidates) > 1

    last_status, last_body, last_err = 0, "", ""
    last_rc = 1

    def _try(try_model):
        return (try_model, _nim_post_chat(
            base_url, api_key, try_model, messages,
            max_tokens=max_tokens, temperature=temperature, timeout=per_timeout,
        ))

    def _format(try_model, status, body, err):
        if 200 <= status < 300:
            try:
                data = json.loads(body)
            except json.JSONDecodeError:
                return None
            choice = ((data.get("choices") or []) or [{}])[0]
            text = (choice.get("message") or {}).get("content", "") or ""
            sid = resume_sid or "nim-" + str(hash((try_model, prompt[:80], key_src)) & 0xfffffff)
            return ProviderResult(rc=0, session_id=sid, text=text, raw=body[:5000])
        return None  # 失败；调用方继续

    if concurrent:
        # 并发试探：首个成功返回即采纳（其余请求自然被丢弃）
        import concurrent.futures
        # 适度裁剪避免无意义拉爆：最多 3 个并发候选
        cohort = candidates[:3]
        # 外层等待 = per_timeout + 5s（让首个返回有时间）
        outer_timeout = per_timeout + 5
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(cohort)) as ex:
            futures = {ex.submit(_try, m): m for m in cohort}
            try:
                for fut in concurrent.futures.as_completed(futures, timeout=outer_timeout):
                    try:
                        try_model, (status, body, err) = fut.result()
                    except Exception as e:
                        continue
                    result = _format(try_model, status, body, err)
                    if result:
                        _log(f"NIM concurrent: hit {try_model!r}")
                        return result
                    last_status, last_body, last_err = status, body, err
                    last_rc = 124 if status == 0 else 1
            except concurrent.futures.TimeoutError:
                _log(f"NIM concurrent: outer timeout ({outer_timeout}s)")
        # 兜底走串行路径
        for try_model in candidates:
            status, body, err = _try(try_model)[1]
            result = _format(try_model, status, body, err)
            if result:
                return result
            last_status, last_body, last_err = status, body, err
    else:
        for idx, try_model in enumerate(candidates):
            if idx > 0:
                _log(f"NIM fallback: trying {try_model!r} after {candidates[idx-1]!r} failed")
            _, (status, body, err) = _try(try_model)
            result = _format(try_model, status, body, err)
            if result:
                return result
            if status in (401, 403):
                return ProviderResult(
                    rc=127, session_id=resume_sid,
                    text=f"NIM auth error {status}: {body[:300]}",
                    raw=body[:1000],
                )
            if status == 429:
                return ProviderResult(rc=124, session_id=resume_sid,
                                      text="NIM 429 rate-limited",
                                      raw=body[:1000])
            last_status, last_body, last_err = status, body, err
            last_rc = 124 if status == 0 else 1

    # 所有候选都失败
    if last_status == 0:
        return ProviderResult(
            rc=124, session_id=resume_sid,
            text="NIM network error: " + (last_err[:200] or "unknown"),
            raw=last_err,
        )
    return ProviderResult(
        rc=last_rc or 1, session_id=resume_sid,
        text=f"NIM all candidates failed; last http {last_status}: {last_body[:200]}",
        raw=last_body[:1000],
    )


# --------------------------------------------------------------------------- #
# MiniMax API 调用 (minimax.cn / minimax-global) v2 chatcompletion
# --------------------------------------------------------------------------- #

def _minimax_post_chat(base_url, api_key, model, messages,
                       max_tokens=2048, temperature=0.2, timeout=120,
                       thinking_disabled=True):
    """调用 MiniMax v2 /v1/text/chatcompletion_v2。
    返回 (status, body_text, err_text)。"""
    import urllib.request
    import urllib.error
    url = (base_url or "https://api.minimaxi.com/v1").rstrip("/") \
        + "/text/chatcompletion_v2"
    payload = {
        "model": model,
        "messages": messages,
        "max_tokens": int(max_tokens),
        "temperature": float(temperature),
    }
    if thinking_disabled:
        payload["thinking"] = {"type": "disabled"}
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url, data=data, method="POST",
        headers={
            "Authorization": "Bearer " + api_key,
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
    )
    ctx = None
    if os.environ.get("CICR_TLS_INSECURE") in ("1", "true", "yes"):
        import ssl
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
    opener_args = {"timeout": timeout}
    if ctx is not None:
        opener_args["context"] = ctx
    try:
        with urllib.request.urlopen(req, **opener_args) as resp:
            return resp.status, resp.read().decode("utf-8", errors="replace"), ""
    except urllib.error.HTTPError as e:
        try:
            body = e.read().decode("utf-8", errors="replace")
        except Exception:
            body = str(e)
        return e.code, body, str(e)
    except urllib.error.URLError as e:
        return 0, "", "URLError: " + str(e.reason)
    except Exception as e:
        return 0, "", "Exception: " + type(e).__name__ + ": " + str(e)


def run_minimax(prompt, model="MiniMax-M3",
                base_url="https://api.minimaxi.com/v1",
                resume_sid=None, timeout=120, **kwargs):
    """调用 MiniMax chatcompletion_v2。返回 ProviderResult。
    支持同 provider 内多模型回退链（kwargs: fallback_models = [(base_url, model), ...]）。
    5xx/429/网络错触发回退；401/403 立即 rc=127（鉴权失败跨模型无意义重试）。"""
    api_key, key_src = _load_provider_key("minimax")
    if not api_key:
        return ProviderResult(
            rc=127, session_id=resume_sid,
            text="MINIMAX api_key not found (set $MINIMAX_API_KEY or config/secret.yaml)",
            raw="",
        )

    candidates = [(base_url, model)]
    fb = kwargs.get("fallback_models")
    if isinstance(fb, (list, tuple)):
        for item in fb:
            if isinstance(item, tuple) and len(item) == 2:
                candidates.append((item[0] or base_url, item[1]))

    sp = kwargs.get("system_prompt")
    messages = []
    if sp:
        messages.append({"role": "system", "content": sp})
    messages.append({"role": "user", "content": prompt})

    max_tokens = int(kwargs.get("max_tokens", 4096))
    temperature = float(kwargs.get("temperature", 0.2))
    thinking_disabled = kwargs.get("thinking_disabled", True)

    last_status = 0
    last_body = ""
    last_err = ""

    for idx, (try_url, try_model) in enumerate(candidates):
        if idx > 0:
            _log(f"MINIMAX fallback: {try_model!r} via {try_url}")
        status, body, err = _minimax_post_chat(
            try_url, api_key, try_model, messages,
            max_tokens=max_tokens, temperature=temperature,
            timeout=timeout, thinking_disabled=thinking_disabled,
        )
        if status == 0:
            last_status, last_body, last_err = status, body, err
            continue
        if 200 <= status < 300:
            try:
                data = json.loads(body)
            except json.JSONDecodeError:
                last_status, last_body, last_err = status, body, err
                continue
            choice = ((data.get("choices") or []) or [{}])[0]
            msg = choice.get("message", {})
            text = msg.get("content", "") or msg.get("reasoning_content", "") or ""
            sid = resume_sid or "minimax-" + str(hash((try_model, prompt[:80], key_src)) & 0xfffffff)
            return ProviderResult(rc=0, session_id=sid, text=text, raw=body[:5000])
        # 鉴权失败跨模型没意义（同一 key），立即返回
        if status in (401, 403):
            return ProviderResult(
                rc=127, session_id=resume_sid,
                text=f"MINIMAX auth {status}: {body[:300]}",
                raw=body[:1000],
            )
        # 429/5xx：继续尝试下一个候选
        last_status, last_body, last_err = status, body, err
        if status == 429:
            # 速率限制：背刺一次后继续
            time.sleep(0.5)
            continue
        continue

    if last_status == 0:
        return ProviderResult(
            rc=124, session_id=resume_sid,
            text="MINIMAX network: " + (last_err[:200] or "unknown"),
            raw=last_err,
        )
    if last_status == 429:
        return ProviderResult(
            rc=124, session_id=resume_sid,
            text=f"MINIMAX 429 all candidates failed: {last_body[:200]}",
            raw=last_body[:1000],
        )
    if last_status >= 500:
        return ProviderResult(
            rc=124, session_id=resume_sid,
            text=f"MINIMAX server {last_status} all candidates failed: {last_body[:200]}",
            raw=last_body[:1000],
        )
    return ProviderResult(
        rc=last_status // 100 if last_status >= 400 else 1,
        session_id=resume_sid,
        text=f"MINIMAX http {last_status}: {last_body[:200]}",
        raw=last_body[:1000],
    )


# --------------------------------------------------------------------------- #
# 统一调度
# --------------------------------------------------------------------------- #

class ProviderResult:
    """模型调用结果。"""
    def __init__(self, rc, session_id, text, raw):
        self.rc = rc            # 退出码：0=成功 124=超时 127=命令缺失
        self.session_id = session_id
        self.text = text        # 模型输出文本
        self.raw = raw          # 原始输出（用于诊断）

    def __repr__(self):
        return (f"ProviderResult(rc={self.rc}, session_id={self.session_id!r}, "
                f"text_len={len(self.text)})")


def run_model(prompt, role_name="executor", resume_sid=None, timeout=900,
              **kwargs):
    """按角色配置的 provider 调度模型调用。
    返回 ProviderResult。若 provider 未知或角色不存在，返回 rc=127。"""
    role = get_role(role_name)
    if not role:
        return ProviderResult(rc=127, session_id=resume_sid,
                              text=f"role '{role_name}' not found in model_router",
                              raw="")
    provider = role.get("provider", "codebuddy")
    model = role.get("model") or role.get("plan")
    base_url = role.get("base_url")

    # 透传全局 NIM 调优（thresholds.*）
    _, th = load_model_router()
    if "nim_per_call_timeout" in th and "_nim_per_call_timeout" not in kwargs:
        kwargs["_nim_per_call_timeout"] = int(th["nim_per_call_timeout"])
    if "nim_concurrent_fallback" in th and "_nim_concurrent_fallback" not in kwargs:
        kwargs["_nim_concurrent_fallback"] = bool(th["nim_concurrent_fallback"])

    if provider == "codebuddy":
        return run_codebuddy(prompt, model=model or "default-model",
                             resume_sid=resume_sid, timeout=timeout, **kwargs)
    if provider == "codex":
        return run_codex(prompt, model=model, resume_sid=resume_sid,
                         timeout=timeout, **kwargs)
    if provider == "nvidia_nim":
        # NIM 不持久会话；resume_sid 仅做幂等键
        # 把 role.fallback_models 透传为 run_nvidia_nim 的候选链
        if "fallback_models" not in kwargs and isinstance(role.get("fallback_models"), list):
            kwargs["fallback_models"] = role["fallback_models"]
        # 透传 max_tokens（若 role 配置）
        if "max_tokens" not in kwargs and role.get("max_tokens"):
            kwargs["max_tokens"] = int(role["max_tokens"])
        return run_nvidia_nim(
            prompt, model=model or "z-ai/glm-5.2",
            base_url=base_url or "https://integrate.api.nvidia.com/v1",
            resume_sid=resume_sid, timeout=timeout, **kwargs,
        )
    if provider == "minimax":
        # MiniMax API v2 - 国内主力大脑
        # 解析 yaml 中的 fallback_models 字符串列表（格式："provider:model"）
        if "fallback_models" not in kwargs and isinstance(role.get("fallback_models"), list):
            parsed_fb = []
            for entry in role["fallback_models"]:
                if not isinstance(entry, str):
                    continue
                if ":" in entry:
                    fb_provider, fb_model = entry.split(":", 1)
                    fb_provider = fb_provider.strip()
                    fb_model = fb_model.strip()
                    if not fb_provider or not fb_model:
                        continue
                    if fb_provider == "minimax":
                        parsed_fb.append((base_url or "https://api.minimaxi.com/v1", fb_model))
                    else:
                        # 跨 provider：原始字符串保留，run_minimax 用不到
                        # 但 run_model 已会先跑 minimax 主路径，遇到 rc=124 则
                        # 这种情况下外层 task_executor 会走 driver 的 retry，
                        # 我们再手动调 run_model(provider="<fb_provider>")
                        pass
                else:
                    # 简单字符串：当作同 provider 的模型变体
                    parsed_fb.append((base_url or "https://api.minimaxi.com/v1", entry.strip()))
            if parsed_fb:
                kwargs["fallback_models"] = parsed_fb
        if "max_tokens" not in kwargs and role.get("max_tokens"):
            kwargs["max_tokens"] = int(role["max_tokens"])
        return run_minimax(
            prompt, model=model or "MiniMax-M3",
            base_url=base_url or "https://api.minimaxi.com/v1",
            resume_sid=resume_sid, timeout=timeout, **kwargs,
        )
    if provider == "arkcli":
        # arkcli 作为 provider 时，仍通过 codebuddy 调用（arkcli 是套餐而非独立 CLI）
        # model 字段映射到 codebuddy 的 model
        return run_codebuddy(prompt, model=model or "default-model",
                             resume_sid=resume_sid, timeout=timeout, **kwargs)
    return ProviderResult(rc=127, session_id=resume_sid,
                          text=f"unknown provider: {provider}", raw="")
