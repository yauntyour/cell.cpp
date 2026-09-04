<div align="center">

# cell.cpp

**A single-file AI coding agent in modern C++26**

One translation unit, one executable: talk to OpenAI- / Anthropic-compatible endpoints (OpenAI
Chat Completions, OpenAI Responses, and Anthropic messages), let the model read / write / edit /
search / execute inside a strict sandbox, and keep every credential in an encrypted on-disk vault.

[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue?logo=cplusplus)](https://isocpp.org/)
[![Single file](https://img.shields.io/badge/layout-single%20translation%20unit-orange)](cell.cpp)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

```
cell --provider openai --base https://api.openai.com/v1 --model gpt-4o --key sk-***
```

</div>

---

## Table of Contents

- [Features](#features)
- [Quick start](#quick-start)
- [Command-line options](#command-line-options)
- [Slash commands](#slash-commands)
- [Tools](#tools)
- [Security model](#security-model)
- [Sessions and the working-directory model](#sessions-and-the-working-directory-model)
- [Configuration file](#configuration-file)
- [Runtime data layout](#runtime-data-layout)
- [Architecture](#architecture)
- [The agent loop](#the-agent-loop)
- [Streaming UI and keyboard control](#streaming-ui-and-keyboard-control)
- [Logging, signals and exit codes](#logging-signals-and-exit-codes)
- [Self-test](#self-test)
- [Implementation notes](#implementation-notes)

## Features

| | Feature | Where it lives |
|---|---|---|
| 🤖 | **Three API styles** — OpenAI Chat Completions (`{base}/chat/completions`), OpenAI Responses (`{base}/v1/responses`) and Anthropic (`{base}/v1/messages`), all streaming and non-streaming, with per-provider HTTP(S) proxy support | `cell::llm`, `cell::net` |
| 🧩 | **Provider registry** — any number of named endpoints; model lists are fetched live from the provider, only the active model name is persisted | `cell::config` |
| 🔧 | **7 built-in tools** — `ls`, `read`, `write`, `edit`, `rg`, `exec`, `find` | `cell::box`, `cell::tools` |
| 🛡️ | **Three sandbox modes** — `read-only`, `edit-only`, `full-access`; network egress is denied in *every* mode and credential/secret files are off limits everywhere | `cell::box::check_exec` |
| 🧨 | **Prompt-injection sanitizer** — every `exec` result is scanned for command-override fingerprints, robust to homoglyphs, zero-width marks, punctuation-joined tokens and multi-line splits | `cell::box::sanitize_output` |
| 🔐 | **Encrypted credential vault** — Argon2id key derivation + AES-256-GCM (XChaCha20-Poly1305 fallback), `sodium_malloc`/`sodium_memzero` secret buffers | `cell::encrypt` |
| 💬 | **Per-directory sessions** — session ids embed a hash of the working directory; switching a session follows its cwd | `cell::chat` |
| 🧠 | **Chain of thought** — `/think` with 5 levels (off/low/med/high/max); OpenAI streams `reasoning_content`, Anthropic streams `thinking_delta`, both rendered dim; reasoning content is persisted in sessions | `cell::llm` |
| 📦 | **Skills** — Markdown files with YAML front matter under `.cell/skills/` (recursive, directory-style supported), injected as system messages | `cell::skills` |
| 📊 | **Usage statistics** — per-model and per-session request/token/character counters with prompt-cache hit rate | `cell::stats` |
| ⚡ | **Async I/O** — session/config/stats writes are coalesced and flushed by one background thread; a dynamic worker pool runs read-only tool calls concurrently | `cell::async_io`, `cell::sys::thread_pool` |
| 🖥️ | **Cross-platform** — Windows (Job Objects, `cmd.exe`, UTF-8 codepage, virtual terminal) and POSIX (`fork`/`exec`, `termios`); all OS-specific code is isolated in one namespace | `cell::plat` |
| 🧪 | **Self-test** — a few hundred assertions covering the sandbox, editor, sanitizer, crypto, config, sessions, skills, stats and the tool registry | `--selftest` |

## Quick start

### Prerequisites

- A **C++26 toolchain** with library support for `<format>`, `<expected>`, `<generator>`,
  `<span>`, `<concepts>` and `std::move_only_function` — e.g. GCC 15+ (MSYS2 UCRT64) or a recent
  MSVC/Clang. This is bleeding-edge: use a recent standard library, not an LTS one.
- **libcurl** (any recent backend), **libsodium**, **nlohmann/json** (header-only).
- Recommended package manager: [vcpkg](https://github.com/microsoft/vcpkg)

```bash
vcpkg install curl nlohmann-json libsodium
```

### Build

```bash
# POSIX / MSYS2
g++ -std=c++26 -O2 -o bin/cell cell.cpp -lcurl -lsodium

# MSVC
cl /std:c++latest /O2 /EHsc cell.cpp /Fe:bin\cell.exe /I<deps>\include /link libcurl.lib libsodium.lib
```

### Run

```bash
# first launch: register a provider (the key goes into the encrypted vault)
bin/cell --provider openai --base https://api.openai.com/v1 --model gpt-4o --key YOUR_API_KEY

# Anthropic-style endpoint
bin/cell --provider anthropic --base https://api.anthropic.com --model claude-sonnet-4-20250514 --key YOUR_API_KEY

# any OpenAI-compatible gateway (Ollama, vLLM, One-API, ...)
bin/cell --base http://localhost:11434/v1 --model qwen2.5-coder

# non-interactive: stdin is one message, reply is printed, then exit
echo "explain what this repo does" | bin/cell

# verify the build
bin/cell --selftest
```

Inside the REPL, providers are managed without restarting:

```
/provide add openai:https://api.openai.com/v1 key:YOUR_API_KEY
/models
/model gpt-4o
/think on
```

### Session example

```
cell: cwd=D:\work\demo session=6333a2b6f7084f1a-1787819024 model=openai:Qwen3.8-27B sandbox=full-access
context: injected system_prompt=1740chars
> 还记得前面聊了什么吗
记得呀，咱们前面聊了这些内容：
1. **你让我介绍自己** …
2. **你让我查看当前目录下的项目** —— 我列出了目录内容 …
[2026-08-27 08:31:55] INFO  [llm  ] round=1 model=openai:Qwen3.8-27B stream=true ctx_msgs=8 tok_in=3424 tok_out=393 cache=92.6% time=19.95s ttf=11.06s tools=0
> /sessions
  > cwd: D:\work\demo
    6333a2b6f7084f1a-1787819024 *  messages=7  "你好，请介绍一下你自己"
> /compact
context compacted: aggregated 31 message(s) into 1 summary, 2 message(s) remain
> /quit
```

## Command-line options

```
usage: cell [options]
  --provider NAME             select an existing provider, or create one (style = NAME)
  --base URL                  api base url for the current provider
  --model MODEL               default model name
  --proxy URL                 http(s) proxy for the current provider
  --key KEY                   api key (saved to the encrypted vault)
  --session ID                resume an existing session (switches to its working directory)
  --system TEXT               system prompt
  --sandbox MODE              exec sandbox mode: read-only | workspace-write (default) | full-access | outer-full
  --no-color                  disable colored log output
  --verbose                   enable DEBUG-level log output on console
  --selftest                  run internal self tests
```

| Option | Value | Effect |
|---|---|---|
| `--provider` | name | Selects an existing provider by name; if none matches, a provider is created whose **name doubles as the API style** (`openai` / `anthropic`); for OpenAI-style providers with no explicit style, the API style defaults to `openai-chat`. |
| `--base` | URL | Sets `base` on the current provider. There is **no built-in default base** — an empty base means the provider is unusable until you set one. |
| `--model` | string | Active model name, stored as-is (it need not appear in `/models`; a mismatch only warns). |
| `--proxy` | URL | HTTP(S) proxy for the current provider, credentials may be embedded. `localhost`, `127.0.0.1`, `::1` always bypass it (`CURLOPT_NOPROXY`); HTTPS targets are tunnelled with `CONNECT`. |
| `--key` | secret | Encrypted into `.cell/.crypt` under the id `provider:<name>` and bound to the current provider; the plaintext copy is zeroed (`sodium_memzero`) immediately. |
| `--session` | id | Stored as `cfg.session_id`. See [Implementation notes](#implementation-notes): startup always opens a fresh session, so resume with `/session ID`. |
| `--system` | text | Replaces the system prompt entirely. |
| `--sandbox` | `read-only`/`readonly`, `edit-only`/`edit`, `full-access`/`full` | Sets the exec sandbox mode and persists it as `sandbox_mode`. An unknown value warns and rewrites the stored value to `full-access`. Note: `print_usage` also advertises `workspace-write` / `outer-full` aliases, but only the three modes above are implemented. |
| `--no-color` | flag | Disables ANSI colors (color is only used when stdout is a terminal anyway). |
| `--verbose` | flag | Mirrors `DEBUG` log lines to the console (they always go to the log file). |
| `--selftest` | flag | Runs the built-in test suite in an isolated `.cell-selftest/` directory and exits (`0` = all passed). |

**Load order.** `.cell/config.json` is read first (a parse error is reported as a warning and the
program continues with defaults); legacy config shapes are migrated on the fly. CLI flags are then
applied on top of the loaded settings. On exit the merged configuration is written back, so
`--base` / `--model` / `--proxy` / `--sandbox` changes are persistent.

**API key resolution** (per request, first match wins):

1. the vault entry bound to the current provider (`provider:<name>`);
2. the environment variable `OPENAI_API_KEY` / `ANTHROPIC_API_KEY` (chosen by the provider style);
3. the generic vault entry `api_key`.

If none is available the turn fails with a message explaining how to set one.

**Numeric arguments.** Config fields and tool arguments accept both JSON numbers and quoted numeric
strings (`"log_max_lines": "500"`, `{"offset": "85"}`); garbage or missing values fall back to the
default instead of throwing.

## Slash commands

Input starting with `/` is split on whitespace and handled locally — it is never sent to the model.

| Command | Description |
|---|---|
| `/help` | Print the command list |
| `/provides` | List configured providers (style, api_style, base, proxy, key state, model) |
| `/provide NAME` | Select a provider (persisted immediately) |
| `/provide add openai:URL [api_style:openai-chat\|openai-responses] [key:KEY] [proxy:URL] [name:ALIAS]` | Add **and select** a provider; the `openai:` / `anthropic:` prefix picks the API style and an optional `api_style:` overrides the derived style (a spaced `openai: URL` form is accepted). After adding, the model list is fetched once as a connectivity probe. |
| `/provide rm NAME` | Delete a provider and its vault key; if it was current, selection moves to the first remaining provider and the model name is cleared |
| `/models` | Fetch and print the current provider's model list (`<current>` marks the active one) |
| `/model NAME` | Switch model; warns if the name is not in the fetched list |
| `/think [off\|low\|med\|high\|max]` | Cycle or set chain-of-thought level (persisted); bare `/think` cycles: off → low → med → high → max → off; levels: off (default), low (1024 tokens), med (2048), high (4096), max (8192) |
| `/tool [on\|off]` | Toggle tool calling — off means no tool definitions are sent at all |
| `/sandbox [mode]` | Show or set the exec sandbox mode (persisted); reminds you that network egress is blocked in every mode |
| `/autoallow [on\|off]` | Toggle autoallow mode (persisted, full-access only) — when enabled, the LLM decides whether exec commands run without user confirmation |
| `/sessions` | List saved sessions grouped by working directory (`>` = current cwd, `*` = current session) |
| `/session ID` | Switch to a saved session; **the process cwd follows the session's directory** |
| `/session rm ID` | Delete a session file and its usage record |
| `/usages` | Print per-model and per-session usage statistics (orphaned records are pruned first) |
| `/compact` | Aggregate the conversation into one summary message |
| `/ins TEXT` | Interject a user message and get a response (injects text and triggers one LLM round-trip) |
| `/skills` | List available skills |
| `/skill NAME` | Inject a skill body into the current session as a system message |
| `/save` | Persist the current session now (flushes the async writer) |
| `/clear` | Empty the current session's messages but keep its id; re-injects the system prompt and skill list |
| `/new` | Save the current session, then start a fresh one (old files stay on disk) and re-probe the provider |
| `/exit`, `/quit` | Exit |

**Chain of thought (`/think`).** For OpenAI-style providers nothing extra is sent — reasoning models
already stream `delta.reasoning_content`, and `/think` displays it dim ahead of the answer, so
non-reasoning models are unaffected. For Anthropic-style providers the request body gains
`thinking: {"type":"enabled","budget_tokens":N}` (where N depends on the level: low=1024, med=2048,
high=4096, max=8192; `max_tokens` is raised accordingly) and the streamed `thinking_delta` /
`signature_delta` blocks are rendered dim; the thinking blocks stay in the stored assistant message,
which Anthropic requires when a tool call follows a thinking turn. For OpenAI, reasoning_content is
now stored in the session for persistence across restarts.

**Skills (`/skills`, `/skill`).** A skill is a Markdown file under `.cell/skills/`, discovered only if
it opens with a YAML-style front matter block; the scanner recurses (depth ≤ 6), so directory-style
skills work too:

```markdown
---
name: build-helper
description: helpers for building cell
---
Body instructions injected when the skill is loaded…
```

`name` falls back to the file stem — or to the parent folder name for `SKILL.md`/`README.md` — and
`description` to the first non-empty body line (truncated at ~120 chars); surrounding quotes are
stripped. Whenever valid skills exist, every new session gets a metadata-only "available skills" list
injected as a system message so the model can suggest loading one; `/skill NAME` appends the full body
(front matter removed) as a system message for the rest of the session. Names and descriptions pass
through `display_safe` (control/ANSI stripping) but are not fingerprint-redacted — the heavy scan is
reserved for `exec` output.

**Context compaction (`/compact`).** The first `system` message is kept verbatim; every other
message (skills injection, user, assistant, tool results — each truncated to 400 chars) is sent to
the current model with tools disabled and replaced by a single
`{"role":"system","content":"Here is a summary that captures the previous conversation: …"}`
message. With ≤ 12 messages to aggregate it reports "context already small" and does nothing; if the
summarization call fails, a truncation placeholder is used. The summary is re-run through the
injection sanitizer before it is inserted, and the read-before-edit log is reset because the earlier
`read` results are gone from context. Compaction is itself one LLM request and counts toward usage.

## Tools

Seven tools are registered, with schemas emitted for the active API style
(`{"type":"function","function":{…}}` for OpenAI, `{"name":…,"input_schema":…}` for Anthropic, and
`{"type":"function","name":…,"parameters":…}` for the Responses API).

| Tool | Policy | Arguments | Behaviour |
|---|---|---|---|
| `ls` | Allow | `path`, `page`, `page_size` (≤500) | One level, non-recursive; directories first, then case-insensitive name order; header reports total and page window |
| `read` | Allow | `path` (required), `offset` (0-based lines), `limit` | Whole-file mode is capped at 128M characters and streamed; range mode stops reading as soon as the last requested line is consumed; records the returned line range for the read-before-edit rule |
| `write` | Allow | `path`, `content` | Creates a **new** file only — refuses overwrites (points at `edit`) and refuses when the parent directory is missing (points at `exec: mkdir -p`); seeds the edit cache |
| `edit` | Allow | `path` (required), `mode`, `search`, `content`, `from`, `to` | `replace` (unique SEARCH block → content), `insert` (after the block, or after line `from`), `append`, `delete` (block or line range), `query` (read-only locate). A non-unique `search` aborts and reports every match with context; an identical replace is a no-op |
| `rg` | Allow | `pattern` (required), `path`, `max_results` (≤500), `ignore_case`, `context`, `file_type`, `count_only` | Recursive content search with full regex support; skips hidden entries and `.gitignore`d paths; literal fast path for non-regex patterns; groups hits as `=== file ===` + `line: content`; supports case-insensitive search, context lines, file extension filtering, and count-only mode; 8M-line scan budget; nested/alternation-quantifier regexes and patterns over 200 chars are rejected |
| `exec` | **Ask** | `cmd` (required), `timeout` (default 30s, max 300s), `wd` | Runs the command with a hard timeout that kills the child process tree (exit code `124` on timeout); stdout and stderr are captured separately; when the command fails (exit code != 0), stderr is included in the output under `[stderr]`; use `wd` to set the working directory; the result always ends with `exitcode=N` |
| `find` | Allow | `pattern` (glob), `path`, `name`, `newer_than_hours`, `larger_than_bytes`, `max_results` (≤500) | Find files recursively by glob pattern and/or metadata. When only `pattern` is given, behaves like a recursive glob (e.g. `**/*.test.ts`). Combine with metadata filters to narrow results. Returns `path  size bytes  mtime (UTC)` per match |

The `glob` tool is folded into `find`: `find` takes a `pattern` glob and/or `name`/`newer_than_hours`/
`larger_than_bytes` metadata filters.

**Execution scheduling.** Within one assistant turn, all `Policy::Allow` read-only calls
(`ls`/`read`/`rg`/`find`) are dispatched **concurrently** on the shared worker pool; `write`
and `edit` are deliberately deferred to a second, **sequential** pass so that same-message reads
always complete first (read-before-edit) and two edits of one file never race; `exec` runs
sequentially after an interactive confirmation. Results are appended to the transcript in the
original `tool_call` order.

**Read-before-edit rule.** Paths are canonicalized (`weakly_canonical`, lowercased on Windows) and
the line ranges returned by read tools are logged. `edit` refuses to touch any line not covered by a
previously read range, and the log is cleared whenever the visible context changes (`/clear`, `/new`,
`/session`, `/compact`).

**File cache.** `edit` reads through a `(size, mtime)`-validated cache keyed by canonical path, so
consecutive edits of one file skip the disk while an external writer is always picked up.

## Security model

Five independent layers, from the path down to the bytes shown on your screen.

### 1. Sandbox modes

Set with `/sandbox [mode]`, `--sandbox MODE` or the `sandbox_mode` config key. Tools are admitted or
refused by mode before any command runs:

| Mode | Allowed tools | `exec` |
|---|---|---|
| `read-only` | `read`, `rg`, `find`, `ls` (and anything run read-only) | **blocked entirely** |
| `edit-only` | `read`/`rg`/`find`/`ls` **plus** `write`/`edit` | allowed, subject to gates 2–3 |
| `full-access` (default) | all tools | allowed, subject to gates 2–3 |

Network egress is denied in **all three** modes, and the sensitive-path rules in layer 2 apply
everywhere. `exec` is the only tool gated by a human confirmation prompt (layer 4); in `read-only`
mode `exec` is refused before the prompt is ever shown.

### 2. Path gate — `check_path` / `is_sensitive_path`

- any `..` in the argument is rejected;
- everything under the `.cell` runtime directory is off limits (vault, `.key`, `config.json`,
  sessions, logs) — **except** `.cell/skills/`, which the skill system legitimately reads;
- well-known credential stores are blocked anywhere on disk: `~/.ssh/id_{rsa,ed25519,ecdsa,dsa}`,
  `~/.aws/{credentials,config}`, `~/.netrc`, `~/.npmrc`, `~/.pypirc`, `~/.git-credentials`,
  `~/.git/config`, `~/.git/hooks`, `~/.config/gh/hosts.yml`, `~/.docker/config.json`,
  `~/.kube/config`, `~/.m2/settings.xml`, `~/.gradle/gradle.properties`;
- paths are canonicalized with `weakly_canonical` first (symlinks/junctions resolved), so a link
  pointing at a credential file is blocked under its literal name too.

Only the `path`/`dirpath` field is checked for `write`/`edit` — code content may legitimately
contain `>`, `|` or `..`.

### 3. Command gate — `check` / `check_exec`

The sandbox is **path-only**: the command string is checked for path traversal (`..`) and for
sensitive paths (`/.crypt`, `/.key`, `/config.json`, `/sessions/`, `/logs/` and the same credential
store names listed above). It does **not** attempt to block network egress by parsing command names,
decode encoded payloads, or forbid shell operators — the threat model assumes an untrusted *agent*
running inside a trusted host, where `exec` commands are confirmed by the user (or autoallowed in
full-access). In `read-only` mode `exec` is refused outright. Note also that `exec` can still run
interpreters such as `python3 -c "exec(base64…)"` or `cmd /c "echo …"`, because the gate only
inspects paths, not inline code.

High-risk commands that pass the sandbox still demand a **second** confirmation: recursive/forced
deletes (`rm -rf`, `rm -r -f`, `del /s`, `rmdir /s`, …), permission changes (`chmod`, `chown`,
`sudo`, `cacls`, `icacls`, `takeown`) and git operations that can trigger hooks or rewrite history
(`commit`, `merge`, `rebase`, `cherry-pick`, `am`, `apply`, `checkout`, `switch`, `stash`, `clean`,
`reset`, `restore`).

### 4. Human confirmation

`exec` is the only `Ask` tool: `allow exec({…})? [y/N]` — the argument is printed through
`display_safe`, so injected JSON cannot erase the prompt or fake an approval. When **autoallow mode**
is enabled (`/autoallow on`, full-access sandbox only), the LLM alone decides whether exec commands
run without user confirmation — sandbox checks (sensitive paths) still apply. A rejected or blocked
call ends the current agent run instead of letting the model retry it.

### 5. Output hardening

- **`sanitize_output`** (applied to `exec` results, and to `exec`-tagged results reloaded from disk):
  truncates to a byte cap (128 KiB), then redacts line-by-line against ~50 command-override
  fingerprints plus three regex families (verb + filler + `instructions|rules|system prompt|sandbox|
  safety`, `you are now …`, `no longer bound …`). Matching runs on a flattened form of each line:
  UTF-8 decoded, fullwidth/Latin-1/Cyrillic/Greek homoglyphs folded to ASCII, zero-width and bidi
  marks dropped, punctuation collapsed to spaces, and windows of up to 6 adjacent lines joined so
  split fingerprints still match. The tool-output wrapper framing lines are never redacted (they
  carry the `tool="exec"` marker used on reload).
- **`wrap_tool_output`**: every result is wrapped in an explicit untrusted-data boundary tag that
  carries the tool name and a path/command marker, with the attributes sanitized and any occurrence
  of that closing tag inside the body escaped, so injected content cannot forge a nested "authorized"
  tool block or break out of the wrapper.
- **`truncate_output`**: non-exec results get a size cap (512 MiB) without the injection scan.
- **`display_safe`** (single-line, ANSI-stripped) for confirmation prompts and skill metadata;
  **`console_safe`** (multi-line, ANSI-stripped) for the cyan console echo of tool output, which is
  additionally capped at 16 KiB.

### 6. Credential vault

`.cell/.crypt` is a JSON envelope (`version: 2`) holding a random base64 salt and, per key, a
`{nonce, ct}` pair. The AEAD key is derived with **Argon2id** (moderate ops/memory limits) from:

- the `CELL_VAULT_PASSPHRASE` environment variable if set, otherwise
- `.cell/.key` — 32 random bytes, auto-generated on first use (base64 on disk).

Encryption uses **AES-256-GCM** when the CPU provides AES-NI, transparently falling back to
**XChaCha20-Poly1305**. Secrets are held in `secure_string` (`sodium_malloc` buffers, zeroized on
destruction, constant-time comparison); the derived key is wiped in the vault destructor. Plaintext
never appears in the vault file — the self-test asserts this.

## Sessions and the working-directory model

- Each session id is `<cwd-key>-<unix-seconds>`; the cwd key is the first 16 hex characters of the
  SHA-256 of the normalized absolute working directory (lowercased on Windows).
- Files live at `.cell/sessions/<cwd-key>/<id>.json` and record `id`, `cwd` and the full message
  array. `.cell/sessions/sessions.json` is the hash → path index that lets every group be resolved
  back to a real directory; `/sessions` groups by it and marks the current cwd with `>`.
- `/session ID` and startup resume logic **follow the session's cwd** (`current_path` + cache
  invalidation), so tools keep operating on the project the conversation belongs to.
- `/new` keeps the old file (revisitable), `/clear` keeps the id, `/session rm` deletes both the file
  and its usage record; orphaned usage records are pruned at startup and on `/usages`.
- Legacy flat `.cell/sessions/<id>.json` files are migrated once at startup into the current cwd
  group with a rewritten id and `cwd` field.
- Session, config and index writes go through `cell::async_io::file_writer` (coalesced per path, one
  background thread). Commands that read those files back (`/save`, `/sessions`, `/session`,
  `/compact`, exit) call `flush()` first as a durability barrier; the signal handler and the RAII
  exit guard do the same.

## Configuration file

`.cell/config.json` (written with `dump(2)`, so it is hand-editable):

```json
{
  "providers": [
    { "name": "openai", "style": "openai", "api_style": "openai-chat", "base": "https://api.openai.com/v1",
      "key": "provider:openai", "proxy": "http://user:pass@host:8080" },
    { "name": "claude", "style": "anthropic", "api_style": "anthropic", "base": "https://api.anthropic.com" }
  ],
  "current_provider": "openai",
  "current_model": "gpt-4o",
  "think_level": 0,
  "tools": true,
  "sandbox_mode": "full-access",
  "autoallow": false,
  "system": "You are a helpful assistant.…",
  "session": "6333a2b6f7084f1a-1787819024",
  "log_max_lines": 1000,
  "thread_pool_size": 16,
  "active_sessions": { "6333a2b6f7084f1a": "6333a2b6f7084f1a-1787819024" }
}
```

| Field | Default | Meaning |
|---|---|---|
| `providers[]` | *(empty)* | One entry per endpoint: `name` (unique id), `style` (`openai` \| `anthropic`), `api_style` (`openai-chat` \| `openai-responses` \| `anthropic`), `base`, `key` (vault id, not the secret), `proxy`. Models are **not** stored here. |
| `current_provider` | first entry | Active provider when empty |
| `current_model` | — | Active model name |
| `think_level` | `0` | Chain-of-thought level: 0=off, 1=low(1024), 2=med(2048), 3=high(4096), 4=max(8192). Legacy `think: true` maps to level 2. |
| `tools` | `true` | Tool calling enabled |
| `sandbox_mode` | `full-access` | See [sandbox modes](#1-sandbox-modes) |
| `autoallow` | `false` | LLM decides whether exec commands run (only effective in full-access) |
| `system` | short assistant prompt | System prompt |
| `log_max_lines` | `1000` (min 10) | `logs/cell.log` is trimmed to its tail on every startup |
| `thread_pool_size` | `16` (clamped 1–16) | Max concurrent read-only tool workers; the pool spawns lazily and idles with zero workers |
| `active_sessions` | `{}` | cwd key → last active session id |

**Legacy migration.** A flat `{"provider","base","model","key","proxy"}` object, or a
`{"models":[…],"current_model":<index>}` array, is rewritten into the provider list on load
(duplicate endpoints are merged, names are de-duplicated as `style`, `style2`, …).

## Runtime data layout

`.cell` is anchored to **the directory containing the cell executable** (`GetModuleFileNameW` /
`/proc/self/exe`), not the current working directory — so `bin/cell.exe` uses `bin/.cell/` no matter
where you launch it from.

```
.cell/
├── config.json           # settings above
├── .crypt                # encrypted vault (version 2: salt + per-key {nonce, ct})
├── .key                  # 32-byte master secret (base64) — unless CELL_VAULT_PASSPHRASE is set
├── usages.json           # {"sessions": {id: {...}}, "models": {"provider:model": {...}}}
├── skills/               # *.md with YAML front matter, scanned recursively (depth <= 6)
│   └── suite/core/SKILL.md
├── logs/
│   └── cell.log          # [UTC timestamp] LEVEL [cat  ] message, trimmed at startup
└── sessions/
    ├── sessions.json     # cwd hash -> cwd path index
    └── 6333a2b6f7084f1a/ # one directory per working directory
        └── 6333a2b6f7084f1a-1787819024.json
```

Usage records accumulate `requests`, `messages`, `input_chars`, `output_chars`, `input_tokens`,
`output_tokens`, `total_tokens`. The prompt **cache hit rate** shown in the `[llm]` log line is
computed from `usage.prompt_tokens_details.cached_tokens / prompt_tokens` (OpenAI) or
`cache_read_input_tokens / (cache_read_input_tokens + input_tokens)` (Anthropic).

## Architecture

Everything is in `cell.cpp`, split into namespaces with a strict dependency direction: only
`cell::plat` knows about OS APIs, only `cell::net` knows about curl, only `cell::encrypt` knows about
libsodium.

```
cell::async_io   coalescing background file writer (submit / flush)
cell::plat       OS shims: spawn_cmd (timeout + process-tree kill), is_tty, init_console,
                 peek_key (Esc cancel), executable_dir, restore_console
cell::text       zero-copy line generator, trim, BOM strip, display_safe / console_safe
cell::box        the sandbox + every tool implementation: check_path / check / check_exec,
                 is_high_risk, sanitize_output, wrap_tool_output, truncate_output,
                 rg / find / list_dir / read / write / edit,
                 gitignore matcher, walk_entries (lazy DFS), read-before-edit log, file cache
cell::net        curl transport: perform / CURL_post / CURL_stream_post / CURL_get,
                 RAII header_list, OpenAI-style error body extraction
cell::sys        print/println/eprintln/pprintln, structured logger + rotation, exception +
                 source_location, scoped_exit, dynamically-scaling thread_pool, signal handlers
cell::config     provider registry, settings, load/save + legacy migration
cell::encrypt    base64, secure_string, Argon2id + AES-256-GCM vault
cell::tools      Policy (Deny/Ask/Allow), tool base class, callable_tool (approval + gates)
cell::llm        SSE parsers (generator + incremental feed), OpenAI / OpenAIResponses / Anthropic clients
cell::chat       session (per-cwd persistence) and history (in-memory session map)
cell::skills     front-matter parser, recursive scanner, metadata prompt
cell::stats      usage counters in .cell/usages.json
```

## The agent loop

```
user message ──▶ LLM (stream) ──▶ tool_calls? ──▶ pass 1: read-only tools, concurrent
                    │                              pass 2: write/edit (sequential), exec (confirm)
                    │                                   │
                    │      results sanitized + wrapped ──▶ appended to transcript ──▶ next round
                    └──▶ no tool calls ──▶ final answer ──▶ session persisted
```

There is no fixed round cap: the loop continues until the model answers without tool calls, a call is
blocked/rejected, the user cancels, or an error occurs. Each round logs `round=`, context size, token
counts, cache hit rate, total time and time-to-first-token. After every user turn the session and
config are persisted.

## Streaming UI and keyboard control

- `> ` prompt, `reply> ` prefix for streamed answers; the reasoning stream is printed **dim**, the
  answer **plain**, tool results and their echo **cyan**.
- While waiting for the first token a `⏳ Ns` spinner is refreshed every 500 ms; afterwards a
  `~N tok` counter is refreshed on line boundaries.
- **Esc** during a stream cancels it: the partial reply is kept in the transcript and `[cancelled]`
  is printed. (Implemented by `cell::plat::peek_key`, which temporarily puts stdin in raw mode.)
- **Ctrl+C** (and `SIGABRT`/`SIGFPE`/`SIGILL`/`SIGSEGV`) runs the persistence hook — save session,
  save config, flush async writes — restores the terminal and exits with the signal number.
- Non-interactive mode (stdin is not a tty) reads all of stdin as one message, requires a configured
  provider, runs the agent loop once and exits.

## Logging, signals and exit codes

Log lines are `[YYYY-MM-DD HH:MM:SS] LEVEL [cat  ] message` (UTC). Categories include `boot`, `core`,
`llm`, `tool`, `sess`, `ctx`, `cmd`, `user`, `probe`, `provider`, `model`, `think`, `sandbox`,
`skill`, `stats`, `vault`. Only `llm` and `tool` activity is mirrored to the console (`ERROR` always
is; `DEBUG` only with `--verbose`), so the terminal stays readable while the file keeps everything.
Writes are buffered in 16 KiB chunks and flushed immediately for `ERROR`.

| Situation | Exit code |
|---|---|
| Normal exit, or `--selftest` passed | `0` |
| Unknown option / missing option value | `1` |
| Non-interactive run with no provider configured | `1` |
| Fatal `cell::sys::exception` or unhandled `std::exception` (logged, then the RAII guard persists state) | `1` |
| Terminated by a caught signal | the signal number |

A `set_terminate` handler records the type of any escaping exception, and a `set_new_handler` records
out-of-memory before aborting.

## Self-test

```bash
cell --selftest
```

Runs against a throwaway `.cell-selftest/` root (removed afterwards) and covers: high-risk and
sandbox decisions in all three modes, path/traversal/symlink/credential blocking, the injection
sanitizer (case, `\r`, zero-width, fullwidth, Cyrillic/Greek, accented, split-across-lines,
punctuation-joined, paraphrases, oversized output), `wrap_tool_output` forgery resistance,
read/write/`write_new`/`edit` (all five modes, ambiguity, partial-read coverage, no-ops),
`rg`/`find`/`ls` semantics and guards, exec timeouts and exit codes, quoted numeric arguments,
the tool registry and its policies, 16-way concurrent read-only tool calls, incremental SSE parsing
and buffer compaction, the lazy directory walker, thread-pool job accounting, logger rotation, vault
round-trip and persistence, config save/load/migration/error handling, session grouping, `/new`
semantics, the cwd index, load-time re-sanitization of exec results, skill discovery (including
directory-style) and usage statistics. Prints `selftest OK` / `selftest FAILED`.

## Implementation notes

A few places where the code and its own help text/comments differ, or where behaviour is
intentionally simpler than it looks:

- `print_usage` and `print_help` still advertise `workspace-write` / `outer-full` sandbox aliases; the
  values actually implemented are `read-only` / `edit-only` / `full-access` (plus the `readonly` /
  `edit` / `full` aliases). An unknown value warns and rewrites the stored value to `full-access`.
- The sandbox is **path-only**: `box::check_exec` / `box::check` do **not** parse command names,
  decode encoded payloads, or block network egress by binary name — they only reject path traversal
  and sensitive paths. Network egress is "denied" in the sense that there is no whitelist of allowed
  commands; `exec` is gated instead by the sandbox mode, a human confirmation (or autoallow), and the
  high-risk second-confirmation list.
- Startup always opens a **fresh** session (`history::now()` under the `"current"` key);
  `--session` / the `session` field are persisted but not used to resume automatically — resume with
  `/session ID`.
- The built-in default `sandbox_mode` in `settings` is `full-access`, so a missing `config.json` starts
  wide open; the `/sandbox` and `--sandbox` help text still mention `workspace-write`/`outer-full`
  defaults that are not implemented.
- The comment above `cwd_id()` says "sha3-256"; the implementation uses `crypto_hash_sha256`
  (SHA-256, truncated to 16 hex characters).
- `exec` is the only tool whose output is scanned for injection fingerprints; the other tools are
  considered sandboxed at call time and get a size cap only.
- The tool-output wrapper tag is written with an escaped forward slash in the source; the escape
  collapses to a plain slash at runtime, so the marker that reaches the transcript is an opening tag
  carrying the `tool` and `path` attributes, and the reload-time sanitizer keys off the `exec` value of
  that attribute.

## License

MIT — see [LICENSE](LICENSE).
