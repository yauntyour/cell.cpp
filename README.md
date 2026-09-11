<div align="center">

# cell.cpp

**A single-file AI coding agent in modern C++26**

A lightweight AI coding agent written in modern C++26. Connect to OpenAI or Anthropic, chat with your codebase, and let the LLM read, write, edit, and execute — all sandboxed.

[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue?logo=cplusplus)](https://isocpp.org/)
[![Single file](https://img.shields.io/badge/layout-single%20translation%20unit-orange)](cell.cpp)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

</div>

```bash
shell> cell
cell: cwd=path/to/ session=82db5d5997826cfd-1788623888 model=Z.AI:glm-5.3-flash sandbox=full-access
context: loaded 1 message(s) from disk
> 介绍一下自己，以及你能干什么   
The user is asking me to introduce myself and what I can do, in Chinese. Let me respond in Chinese since they asked in Chinese.

I'm an AI assistant operating in a sandboxed environment with various file and shell tools. Let me give a helpful introduction covering:
1. Who I am
2. What tools/capabilities I have
3. What I can help with

I should be accurate about my tools:
- ls: list directory entries
- read: read text files
- write: create new files
- edit: modify existing files (replace/insert/append/delete/query)
- rg: search file contents with regex
- exec: run shell commands (sandboxed, no network egress, high-risk commands need confirmation)
- find: find files by glob pattern/metadata

I can also just chat and answer questions generally. Let me write a nice introduction in Chinese. I shouldn't make it too long, but should be informative. Maybe use some formatting like a list.
你好！很高兴见到你 👋

## 我是谁

我是一个 AI 编程助手（基于 GLM 大模型），运行在一个**沙盒化的工作环境**中。与普通聊天机器人不同的是，我可以直接操作你所在的这个文件系统，真正地"动手"干活，而不只是纸上谈兵。

## 我能做什么

### 📁 文件操作
- **浏览**：列出目录内容、按名称/大小/修改时间递归查找文件
- **读写**：读取文本文件、创建新文件
- **编辑**：精确修改已有文件（替换、插入、删除、追加），支持大文件的分段读取和局部修改

### 🔍 代码搜索与分析
- 用正则表达式递归搜索整个代码库的内容
- 快速定位函数定义、引用、配置项等

### 💻 执行命令
- 运行 shell 命令（如编译、测试、运行脚本、安装依赖）
- 沙盒安全限制：网络请求类命令（curl、git push 等）会被拦截，高风险命令需要二次确认

### 🧠 通用智力
- 写代码、改 bug、重构、写测试、代码审查
- 解释技术概念、设计方案、撰写文档
- 当然，也可以纯聊天答疑 😄

## 典型使用场景

| 场景 | 示例 |
|------|------|
| 熟悉陌生代码库 | "帮我看看这个项目的结构" |
| 修 bug | "测试失败了，帮我找原因并修复" |
| 开发功能 | "给这个项目加一个 XX 功能" |
| 项目脚手架 | "从零搭建一个 XX 项目" |
| 自动化 | "写个脚本批量处理这些文件" |

---

想开始的话，可以直接告诉我你的需求，或者让我先 `ls` 看看当前目录里有什么。你今天想做点什么？
[2026-09-05 16:03:46] INFO  [llm  ] round=1 model=Z.AI:glm-5.3-flash stream=true ctx_msgs=2 tok_in=1640 tok_out=603 cache=0.0% time=20.08s ttf=5.17s tools=0 attempts=1

> 
```

---

## Table of Contents

- [Features](#features)
- [Quick start](#quick-start)
- [Command-line options](#command-line-options)
- [Slash commands](#slash-commands)
- [Tools](#tools)
- [Teamwork](#teamwork)
- [Notices](#notices)
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
| --- | --- | --- |
| 🤖 | **Three API styles** — OpenAI Chat Completions (`{base}/chat/completions`), OpenAI Responses (`{base}/v1/responses`) and Anthropic (`{base}/v1/messages`), all streaming and non-streaming, with per-provider HTTP(S) proxy support | `cell::llm`, `cell::net` |
| 🧩 | **Provider registry** — any number of named endpoints; model lists are fetched live from the provider, only the active model name is persisted | `cell::config` |
| 🔧 | **9 built-in tools** — `ls`, `read`, `write`, `edit`, `rg`, `exec`, `find`, `tw`, `notice` | `cell::box`, `cell::tools` |
| 👥 | **Teamwork** — supervised child agents with configurable workflows (`tw` tool + `/teamworks`): named stages with per-stage dispatch mode, supervisor gates, same-job reuse, background runs with cooperative stops, a per-child provider/model/think level, reuse of earlier children (with or without replaying their transcript), and queryable history & reports | `cell::teamwork` |
| 📣 | **Notices** — a token-free message bus between agent sessions: Teamwork children report progress, the supervisor idles in `/notices wait` and is woken instantly, all without a single LLM round-trip | `cell::notice` |
| 🛡️ | **Three sandbox modes** — `read-only`, `edit-only`, `full-access`; credential/runtime files are off limits in every mode and `exec` is the only tool gated by a confirmation prompt | `cell::box::check_exec` |
| 🧨 | **Prompt-injection sanitizer** — every `exec` result is scanned for command-override fingerprints, robust to homoglyphs, zero-width marks, punctuation-joined tokens and multi-line splits | `cell::box::sanitize_output` |
| 🔐 | **Encrypted credential vault** — Argon2id key derivation + AES-256-GCM (XChaCha20-Poly1305 fallback), `sodium_malloc`/`sodium_memzero` secret buffers | `cell::encrypt` |
| 💬 | **Per-directory sessions** — session ids embed a hash of the working directory; switching a session follows its cwd | `cell::chat` |
| 🗜️ | **Automatic context compaction** — on context-overflow errors (compress + retry once) and after long tool-heavy turns; the compression model can differ from the chat model | `cell::chat`, `compact()` |
| 🧠 | **Chain of thought** — `/think` with 5 levels (off/low/med/high/max); OpenAI streams `reasoning_content`, Anthropic streams `thinking_delta`, both rendered dim; reasoning content is persisted in sessions | `cell::llm` |
| 🔎 | **Numbered reads** — `read` returns every line prefixed with a right-aligned line number, and `rg` groups hits as `=== file ===` + `line: content`, so the model can cite exact lines to `edit` | `cell::box` |
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
/think low
```

## Command-line options

```
usage: cell [options]        # verbatim from print_usage — the --sandbox line overstates the modes
  --provider NAME             select an existing provider, or create one (style = NAME)
  --base URL                  api base url for the current provider
  --model MODEL               default model name
  --proxy URL                 http(s) proxy for the current provider
  --key KEY                   api key (saved to the encrypted vault)
  --session ID                resume an existing session (switches to its working directory)
  --system TEXT               system prompt
  --sandbox MODE              exec sandbox mode: read-only | edit-only | full-access (default)
  --no-color                  disable colored log output
  --verbose                   enable DEBUG-level log output on console
  --selftest                  run internal self tests
```

| Option | Value | Effect |
| --- | --- | --- |
| `--provider` | name | Selects an existing provider by name; if none matches, a provider is created whose **name doubles as the API style** (`openai` / `anthropic`); for OpenAI-style providers with no explicit style, the API style defaults to `openai-chat`. Switching to a **different** provider clears `current_model` — the stored model name belonged to the previous provider — while re-selecting the already-active provider (including the empty `current_provider` case, which means "first provider is active") keeps it; an explicit `--model` on the same command line is applied afterwards. |
| `--base` | URL | Sets `base` on the current provider. There is **no built-in default base** — an empty base means the provider is unusable until you set one. |
| `--model` | string | Active model name, stored as-is (it need not appear in `/models`; a mismatch only warns). |
| `--proxy` | URL | HTTP(S) proxy for the current provider, credentials may be embedded. `localhost`, `127.0.0.1`, `::1` always bypass it (`CURLOPT_NOPROXY`); HTTPS targets are tunnelled with `CONNECT`. |
| `--key` | secret | Encrypted into `.cell/.crypt` under the id `provider:<name>` and bound to the current provider; the plaintext copy is zeroed (`sodium_memzero`) immediately. |
| `--session` | id | Stored as `cfg.session_id`. See [Implementation notes](#implementation-notes): startup always opens a fresh session, so resume with `/session ID`. |
| `--system` | text | Replaces the system prompt entirely. |
| `--sandbox` | `read-only`/`readonly`, `edit-only`/`edit`, `full-access`/`full` | Sets the exec sandbox mode and persists it as `sandbox_mode`. An unknown value warns and rewrites the stored value to `full-access`. |
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
| --- | --- |
| `/help` | Print the command list |
| `/provides` | List configured providers (style, api_style, base, proxy, key state, model) |
| `/provide NAME` | Select a provider (persisted immediately). Switching to a **different** provider resets the model to unset — the stored model belonged to the previous provider — so pick a new one with `/models` + `/model NAME`; re-selecting the active provider keeps its model. After a switch the REPL prints either the kept model or a "no model set" hint. |
| `/provide add API_STYLE:URL [key:KEY] [proxy:URL] [name:ALIAS]` | Add **and select** a provider; the prefix is the API style of that URL (`openai-chat` \| `openai-responses` \| `anthropic`; `openai` is accepted as an alias of `openai-chat`, a spaced `openai: URL` form is also accepted). Selecting the new provider clears any previous model name. After adding, the model list is fetched once as a connectivity probe. |
| `/provide update NAME [base:[API_STYLE:]URL] [key:KEY] [proxy:URL] [name:NEW_NAME]` | Update an existing provider in place. The API style is set together with the base URL it applies to, e.g. `base:anthropic:https://api.anthropic.com`; `base:URL` alone keeps the current style. Only supplied fields change; renaming the provider carries its encrypted vault key to the new vault id, and a requested name that already exists is made unique as `NEW_NAME-N`. |
| `/provide rm NAME` | Delete a provider and its vault key; if it was current, selection moves to the first remaining provider and the model name is cleared |
| `/models` | Fetch and print the current provider's model list (`<current>` marks the active one) |
| `/model NAME` | Switch model; warns if the name is not in the fetched list |
| `/model NAME [api_style:STYLE]` | Optionally switch the current provider's API wire style on the fly (`openai-chat` \| `openai-responses` \| `anthropic`; `openai` is accepted as an alias of `openai-chat`); the new style is persisted in `config.json` and shown by `/model` and `/provides` |
| `/think [off\|low\|med\|high\|max]` | Set the chain-of-thought level (persisted; an unknown level prints the usage and keeps the previous one); levels: off (default), low (1024 tokens), med (2048), high (4096), max (8192); bare `/think` shows the current level and budget |
| `/tool [on\|off]` | Toggle tool calling — off means no tool definitions are sent at all |
| `/sandbox [mode]` | Show or set the exec sandbox mode (persisted); valid values are `read-only`/`readonly`, `edit-only`/`edit`, `full-access`/`full` — anything else is refused with a usage error |
| `/autoallow [on\|off]` | Toggle autoallow mode (persisted, full-access only) — when enabled, the LLM decides whether exec commands run without user confirmation |
| `/sessions` | List saved sessions grouped by working directory (`>` = current cwd, `*` = current session) |
| `/session ID` | Switch to a saved session; **the process cwd follows the session's directory** |
| `/session rm ID` | Delete a session (folder with transcript, Teamwork data, archives) and its usage record |
| `/saved [list]` | List the compaction archives of the current session (`saved/msg-<UTC time>.jsonl` in the session folder) |
| `/saved show NAME` | Display an archived transcript (exact name or unique substring of one) |
| `/saved rm NAME` | Delete an archived transcript |
| `/export [PATH]` | Export the current session transcript as a **self-contained HTML file** (single file, no scripts/external resources; light/dark aware; markdown-lite rendering — headings, lists, quotes, rules, pipe tables, `code`, **bold**, *italic*, links — with tool calls, tool results and reasoning as collapsed sections; default `cell-export-<UTC time>.html` in the working directory) |
| `/export saved NAME [PATH]` | Export one compaction archive to HTML (name resolved like `/saved show`) |
| `/usages` | Print per-model and per-session usage statistics (orphaned session records are pruned first) |
| `/compact` | Archive the full transcript to `saved/msg-<UTC time>.jsonl`, then aggregate the conversation (plus the agent's reasoning) into one system summary message; refuses while the context is small (≤ 12 messages) |
| `/compact auto [on\|off]` | Show or toggle automatic compaction after long agent runs (persisted, default on) |
| `/compact model provider:model` | Route summarization through a specific registered provider/model; `inherit` resets it to the session model |
| `/ins TEXT` | Interject a user message and get a response (injects text and triggers one LLM round-trip) |
| `/skills` | List available skills |
| `/skill NAME` | Inject a skill body into the current session as a system message |
| `/teamworks` | List Teamwork jobs of the current session (see [Teamwork](#teamwork)) |
| `/teamworks all` | List Teamwork jobs of every session in this working directory |
| `/teamworks new` | Create an empty Teamwork job and print its id |
| `/teamworks id:ID bg:none\|prolegomena works:TEXT [provider:P] [model:M] [think:L] [reuse:JOB/worker] [name:NAME]` | Append a child task to a pending job (workflow jobs file it into the last stage) |
| `/teamworks workflow [FILE]` | Load a workflow/job config from a JSON file in the working directory (default `teamwork.workflow.json`; a bare `{stages:[…]}` is accepted) and create the job |
| `/teamworks run ID` | Run a job and print its consolidated report; a gated stage pauses the job — run again to resume |
| `/teamworks run_bg ID` | Run a job in the background on the dedicated runner thread; progress arrives as notices |
| `/teamworks resume ID` | Continue a gated/stopped workflow job from its saved stage |
| `/teamworks stop ID` | Request a cooperative stop (pauses at the next worker round or stage boundary) |
| `/teamworks status ID` | Show one job with its stage plan, gates and progress |
| `/teamworks max [N]` | Show or set the child-agent limit (1–100, persisted) |
| `/teamworks rm ID` / `/teamworks rm ID:worker_N` | Delete a job, or one child task and its report |
| `/teamworks history [JOB] [worker:W] [n:N] [all]` | List past child-agent tasks with status/model/rounds |
| `/teamworks reports [JOB] [worker:W] [n:N] [full] [all]` | Print several child reports (newest first) |
| `/teamworks report JOB:worker` | Print one child's report |
| `/notices` | Peek pending notices of the current session (see [Notices](#notices)) |
| `/notices wait [SEC] [from:X] [topic:Y]` | Block token-free until a matching notice arrives, then deliver pending notices into the conversation (default 60s, max 3600s) |
| `/notices send [SESSION] TEXT` | Deliver a notice to a session (the token after `send` is the target only when it resolves to a session id — exact match or unique prefix; otherwise the text goes to the current session) |
| `/notices drain` | Consume pending notices into the conversation (also happens automatically before every LLM turn) |
| `/notices clear` | Drop pending notices (archived to `seen.jsonl`) |
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
message is split into conversation text and agent reasoning (thinking blocks are summarized in a
second pass), each message truncated to 400 chars, and both parts are sent to the compression model
(`compact_provider` / `compact_model`, falling back to the session model) with tools disabled. The
two summaries are merged into a single
`{"role":"system","content":"# Here is a summary that captures the previous conversation: …"}`
message. With ≤ 12 messages to aggregate it reports "context already small" and does nothing; if a
summarization call fails, a truncation placeholder is used. The summary is re-run through the
injection sanitizer before it is inserted, and the read-before-edit log is reset because the earlier
`read` results are gone from context. Compaction is itself one LLM request per part and counts
toward usage.

**Automatic compaction.** Two triggers, both using the same `compact()` path:

- **On context overflow** — when a request fails with an error matching known OpenAI Chat
  Completions / OpenAI Responses / Anthropic "context length exceeded" phrases (matched
  case-insensitively, e.g. `maximum context length`, `context_length_exceeded`, `prompt is too
  long`), the session is compacted once and the same round is retried against the compacted context
  without counting as a failed attempt (`[auto-compact on context overflow]`).
- **After long agent runs** — when `compact_auto` is on (the default) and a turn recorded
  ≥ 3 tool calls plus reasoning/thinking entries, the context is compacted after the turn
  (`[auto-compact] …`). Disable with `/compact auto off`.

## Tools

Nine tools are registered — `ls`, `read`, `write`, `edit`, `rg`, `exec`, `find`, `tw` and `notice` — with
schemas emitted for the active API style
(`{"type":"function","function":{…}}` for OpenAI, `{"name":…,"input_schema":…}` for Anthropic, and
`{"type":"function","name":…,"parameters":…}` for the Responses API).

| Tool | Policy | Arguments | Behaviour |
| --- | --- | --- | --- |
| `ls` | Allow | `path`, `page`, `page_size` (≤500) | One level, non-recursive; directories first, then case-insensitive name order; each entry is printed as `[dir ] NAME` or `[file] NAME  N bytes`, and the header reports `total`, the page window and the path |
| `read` | Allow | `path` (required), `offset` (0-based lines), `limit` | Whole-file mode is capped at 128M characters and streamed; range mode stops reading as soon as the last requested line is consumed; every returned line is prefixed with a right-aligned 6-column line number (`{:>6}: content`); records the returned line range for the read-before-edit rule |
| `write` | Allow | `path`, `content` | Creates a **new** file only — refuses overwrites (points at `edit`) and refuses when the parent directory is missing (points at `exec: mkdir -p`); seeds the edit cache |
| `edit` | Allow | `path` (required), `mode`, `search`, `content`, `from`, `to` | `replace` (unique SEARCH block → content), `insert` (after the block, or after line `from`), `append`, `delete` (block or line range), `query` (read-only locate). A non-unique `search` aborts and reports every match with context; an identical replace is a no-op; a `search` that ends with a newline the file does not have is retried without it (and the replacement drops its own trailing newline) |
| `rg` | Allow | `pattern` (required), `path`, `max_results` (≤500), `ignore_case`, `context`, `file_type`, `count_only` | Recursive content search with full regex support; skips hidden entries and `.gitignore`d paths; follows a symlink only when it resolves inside the walk root; literal fast path for non-regex patterns; groups hits as `=== file ===` + `line: content`; supports case-insensitive search, context lines (overlapping context is printed once), file extension filtering, and count-only mode; 8M-line scan budget; exponential-backtracking shapes (`(a+)+`, `(a|b){3}`, `a{2}{3}`) and patterns over 200 chars are rejected |
| `exec` | **Ask** | `cmd` (required), `timeout` (default 30s, max 300s), `wd` | Runs the command with a hard timeout that kills the child process tree (exit code `124` on timeout); stdout and stderr are captured separately; when the command fails (exit code != 0), stderr is included in the output under `[stderr]`; use `wd` to set the working directory; the result always ends with `exitcode=N` |
| `find` | Allow | `pattern` (glob), `path`, `name`, `newer_than_hours`, `larger_than_bytes`, `max_results` (≤500) | Find files recursively by glob pattern and/or metadata (a symlink is followed only when it resolves inside `path`; `name` is case-insensitive on Windows; a future `mtime` is never "recent"). When only `pattern` is given, behaves like a recursive glob (e.g. `**/*.test.ts`). Returns `path  size bytes  mtime (UTC)` per match |
| `tw` | **Ask** | `operation`, `id`, `work-type`, `list`, `workflow`, `path`, `from`, `worker`, `provider`, `model`, `think`, `works`, `name`, `reuse_context`, `background`, `limit`, `full`, `all` | Manage Teamwork child-agent jobs and workflows — see [Teamwork](#teamwork) |
| `notice` | Allow | `operation` (send/list/drain/wait/clear), `session`, `from`, `topic`, `body`, `timeout`, `from_filter`, `topic_filter` | Cross-session message bus — see [Notices](#notices) |

The `glob` tool is folded into `find`: `find` takes a `pattern` glob and/or `name`/`newer_than_hours`/
`larger_than_bytes` metadata filters.

**Execution scheduling.** Within one assistant turn, all `Policy::Allow` read-only calls
(`ls`/`read`/`rg`/`find`) are dispatched **concurrently** on the shared worker pool; `write`
and `edit` are deliberately deferred to a second, **sequential** pass so that same-message reads
always complete first (read-before-edit) and two edits of one file never race; `exec` runs
sequentially after an interactive confirmation, and `tw` (Teamwork) also runs in that sequential
pass because it blocks on child agents. Results are appended to the transcript in the
original `tool_call` order.

**Read-before-edit rule.** Paths are canonicalized (`weakly_canonical`, lowercased on Windows) and
the line ranges returned by read tools are logged. `edit` refuses to touch any line not covered by a
previously read range, and the log is cleared whenever the visible context changes (`/clear`, `/new`,
`/session`, `/compact`).

**File cache.** `edit` reads through a `(size, mtime)`-validated cache keyed by canonical path, so
consecutive edits of one file skip the disk while an external writer is always picked up.

## Teamwork

Teamwork runs **supervised child agents**. The main agent creates a *job* holding a list of child
tasks — optionally arranged as a **workflow of named stages** — runs them serially or in parallel,
and gets back a consolidated report. Every child gets an independent message history; children are
restricted to read-only tools in a parallel stage and can never call `tw` themselves, so the
hierarchy stays a tree of depth 1. Children *can* call `notice`, which is how they report progress
to the supervising session mid-run.

Jobs live with the session: `<root>/sessions/<cwd-key>/<session-id>/teamworks.json`, with one
`<session-id>/<job-id>/<worker>.json` transcript per child. Job ids are UTC wall-clock stamps —
`tw-YYYYMMDD-HHMMSS` (a `-N` suffix disambiguates same-second collisions). The store is kept in
memory and mirrored to disk through the async writer, so listing/querying jobs costs no disk I/O.

### Shapes

```jsonc
// tw operation:"new"
{ "work-type": "serial",                  // serial | parallel (parallel = read-only children)
  "provider": "zai",                      // optional job-level defaults, inherited by every child
  "model": "glm-5.3-flash",
  "list": [
    { "works": "audit the net layer", "provider": "claude", "model": "claude-sonnet-4",
      "think": "high",                    // off|low|med|high|max or 0..4
      "background": "prolegomena",        // inject the main conversation as background context
      "name": "net-audit" },
    { "works": "then write regression tests", "reuse": "tw-1788623888-0/net-audit" }
  ] }

// tw operation:"new" with a workflow — stages run in order, each with its own
// agents, dispatch mode, optional stage-level provider/model/think defaults,
// and an optional supervisor gate after the stage
{ "workflow": { "name": "ship-it",
  "stages": [
    { "name": "research", "mode": "parallel",
      "agents": [ { "works": "scan the docs" }, { "works": "scan the code" } ] },
    { "name": "implement", "gate": true, "think": "low",
      "agents": [ { "works": "patch it", "reuse": "worker_0" } ] }   // bare name = same-job reuse
  ] } }
```

A workflow can also be loaded from a file: `tw operation:"new" path:"teamwork.workflow.json"`
(relative to the working directory; the file may hold a full job config or a bare `{stages:[…]}`),
or `/teamworks workflow [FILE]` from the REPL.

| Field (per child) | Meaning |
| --- | --- |
| `works` | task text — required unless `reuse` is given |
| `background` | `none` (default) or `prolegomena` (inject the main conversation as context) |
| `provider` / `model` | run **this child** on a specific provider/model |
| `think` | chain-of-thought level for this child |
| `reuse` | `tw-ID/worker_N` for a past job, or a bare `worker_N` for an earlier-stage worker of the same job — inherits the task config and, by default, the transcript |
| `reuse_context` | `true` (default): replay the earlier conversation; `false`: config only |
| `name` | explicit child name (sanitized for use as a file name) |

Stage fields: `name` (default `stage_N`), `mode` (`serial` default \| `parallel`), `gate` (pause for
the supervisor after this stage), `provider`/`model`/`think` (stage defaults), `agents` (required,
non-empty). A bare `reuse` must name a worker from an **earlier** stage — this is validated when the
job is created.

Model resolution order for a child: its own field → the reused child's field → the stage default →
the job default → the reused job's default → the active provider/model. A `provider` that does not
exist is an error, never a silent fallback. Each child builds its own LLM client and reuses it
across all its rounds, so parallel children never share a curl handle.

### Workflow execution, gates and supervision

- Stages run in order; workers within a stage run serially or in parallel (pool-bounded). After
  every stage the progress so far is persisted (`next_stage`), so a gate pause, a stop or a crash
  never loses completed-stage reports.
- **Gates**: a stage with `gate: true` pauses the job before the next stage (`status=waiting`) and
  posts a notice. The supervisor reviews the stage report and resumes with `tw run`, `/teamworks
  run ID` / `resume ID`, or re-plans with `edit`.
- **Background runs**: `/teamworks run_bg ID` (or `tw run` with `background:true`) executes the
  remaining workflow on a dedicated runner thread while the REPL stays interactive. The supervisor
  can idle in `/notices wait` — zero tokens — and is woken by every stage/worker transition.
- **Stops**: `/teamworks stop ID` requests a cooperative stop; the job pauses at the next worker
  round or stage boundary (`status=waiting`, the interrupted stage re-runs on resume).
- **Boot recovery**: a process exit while a job was running leaves it `running`; the next start
  marks such jobs `failed` so they can be re-run or removed.

### `tw` operations

| Operation | Arguments | Result |
| --- | --- | --- |
| `new` | `work-type` + `list`, or `workflow` (with `stages`), or `path` (config file), optional `provider`/`model`/`think` | creates a job and prints it |
| `run` | `id` (+ optional config override, `background` for a background run) | runs the job / resumes a waiting one, returns the consolidated report |
| `edit` | `id` + new `work-type`/`list`/`workflow` | replaces the config of an uncompleted job (progress resets) |
| `remove` | `id` | deletes a job (agents may not delete completed jobs) |
| `reuse` | `from:"tw-ID/worker_N"`, optional `works`/`provider`/`model`/`think`/`name` | creates a new job seeded from a past child |
| `list` | `all` | lists jobs of this session (or of every session in this working directory) |
| `history` | `id`, `worker`, `limit`, `all` | lists past child tasks: status, provider, model, rounds, tool calls, created |
| `report` | `id` + `worker` | prints one child's report |
| `reports` | `id`, `worker`, `limit`, `full`, `all` | prints many child reports, newest first |

`run` is the only `tw` operation that needs confirmation (`/autoallow` can waive it); the read-only
queries are auto-approved.

### Reports

A finished job records per-child `{status, summary, rounds, tool_calls, provider, model, reused}` in
`summary_reports`. When a report is missing — for instance a child interrupted mid-run — `tw
operation:"report"` falls back to the child's persisted transcript and returns its last assistant
text, so results stay recoverable.

### From the REPL

```
/teamworks                                        list jobs of this session
/teamworks all                                    list jobs of every session in this directory
/teamworks new                                    create an empty job (append children next)
/teamworks workflow [FILE]                        load a workflow config file as a new job
/teamworks id:ID works:... [bg:none|prolegomena] [provider:P] [model:M] [think:L]
                                   [reuse:JOB/worker] [name:NAME]
/teamworks run ID                                 run a job (resumes a gated one) and print its report
/teamworks run_bg ID                              run in the background; progress via notices
/teamworks resume ID                              continue a gated/stopped job from its saved stage
/teamworks stop ID                                request a cooperative stop
/teamworks status ID                              show one job with its stage plan
/teamworks max [N]                                show / set the child limit (1..100)
/teamworks rm ID | /teamworks rm ID:worker_N      delete a job / one child
/teamworks history [JOB] [worker:W] [n:N] [all]   past child tasks
/teamworks reports [JOB] [worker:W] [n:N] [full] [all]
/teamworks report JOB:worker                      one child's report
```

### Scheduling and failure handling

- **serial** jobs run children one after another on the calling thread; **parallel** jobs are
  dispatched through the shared worker pool, so concurrency is bounded by `thread_pool_size`
  (default 16) instead of spawning one thread per child. Workflow stages inherit the same rule per
  stage; background runs execute on their own dedicated runner thread (never on the pool) so a
  pool-hosted run can never deadlock, and are joined at exit.
- A child that throws is captured and reported as `failed`; a job never gets stuck in `running`
  across restarts (boot recovery marks interrupted runs `failed`). Jobs also carry
  `ok / incomplete / failed / stopped` per child, where `incomplete` means the child hit the
  8-round limit without producing a final answer.
- Child runs count toward usage statistics with their real token counts, keyed
  `<job-id>-<child-name>`.

## Notices

The `notice` system is a **message bus for agent sessions**. Any writer — the user from the REPL,
a Teamwork child inside this process, or a second cell process sharing the runtime root — drops a
JSON file into the target session's `notices/pending/` folder (atomic write, sanitized on read:
UTF-8 only, control/ANSI bytes stripped, 16 KB body cap, 500 pending-files cap). Delivered notices
are archived to `notices/seen.jsonl`.

**Waiting is token-free.** `/notices wait` (and the `notice` tool's `wait` operation) block on a
condition variable with a periodic folder re-scan (~0.7 s) — no LLM request is made while idle.
In-process sends wake the waiter instantly; cross-process sends arrive within ~1 s. Pending
notices are also drained automatically before every LLM turn, so a supervisor never has to poll.

- **Send**: `/notices send [SESSION] TEXT`, or the `notice` tool (`from`/`topic` fields give the
  receiver routing hints). Teamwork children report progress this way — `notice` is the one tool
  available to them beyond their sandbox, in every job mode.
- **Automatic progress**: every Teamwork job posts notices for job start/completion/failure, stage
  start/finish, gate pauses, stops, and each worker start/finish (with status and a summary
  snippet), all `from=teamwork/<job-id>[/<worker>]`.
- **Consume**: `/notices drain` (or `notice drain`) delivers pending notices into the conversation
  as context; `wait` delivers what it woke for; `clear` drops them. Delivered = exactly once per
  draining session, thanks to file removal under a drain lock.

## Security model

Six independent layers, from the path down to the bytes shown on your screen.

### 1. Sandbox modes

Set with `/sandbox [mode]`, `--sandbox MODE` or the `sandbox_mode` config key. Tools are admitted or
refused by mode before any command runs:

| Mode | Allowed tools | `exec` |
| --- | --- | --- |
| `read-only` | `read`, `rg`, `find`, `ls` (and anything run read-only) | **blocked entirely** — refused before the confirmation prompt is ever shown |
| `edit-only` | `read`/`rg`/`find`/`ls` **plus** `write`/`edit` | allowed, subject to gates 2–3 |
| `full-access` (default) | all tools | allowed, subject to gates 2–3 |

Network egress is **not** filtered in any mode — the command gate is path-only (see
[layer 3](#3-command-gate--check--check_exec) and [Implementation notes](#implementation-notes)).
The sensitive-path rules in layer 2 apply everywhere. `exec` is the only tool gated by a human
confirmation prompt (layer 4); in `read-only` mode `exec` is refused before the prompt is ever
shown.

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
sensitive paths (`/.crypt`, `/.key`, `/config.json`, `/sessions/`, `/logs/`, both separators, case
folded). It does **not** attempt to block network egress by parsing command names,
decode encoded payloads, or forbid shell operators — the threat model assumes an untrusted *agent*
running inside a trusted host, where `exec` commands are confirmed by the user (or autoallowed in
full-access). In `read-only` mode `exec` is refused outright. Note also that `exec` can still run
interpreters such as `python3 -c "exec(base64…)"` or `cmd /c "echo …"`, because the gate only
inspects paths, not inline code. (The wider credential-store list of layer 2 is enforced by
`check_path` on tool path arguments, not by `check_exec`.)

High-risk commands that pass the sandbox still demand a **second** confirmation: recursive/forced
deletes (`rm -rf`, `rm -r -f`, `del /s`, `rmdir /s`, …), permission changes (`chmod`, `chown`,
`sudo`, `cacls`, `icacls`, `takeown`) and git operations that can trigger hooks or rewrite history
(`commit`, `merge`, `rebase`, `cherry-pick`, `am`, `apply`, `checkout`, `switch`, `stash`, `clean`,
`reset`, `restore`).

### 4. Human confirmation

`exec` is the only `Ask` tool: `allow exec({…})? [y/N]` — the argument is printed through
`display_safe`, so injected JSON cannot erase the prompt or fake an approval. When **autoallow mode**
is enabled (`/autoallow on`, full-access sandbox only), the LLM alone decides whether exec commands
run without user confirmation — sandbox checks (sensitive paths) still apply. Approval refusals
(from the user or autoallow) end the current agent run; sandbox security refusals are returned to
the model as normal feedback.

### 5. Output hardening

- **`sanitize_output`** (applied to `exec` results, and to `exec`-tagged results reloaded from disk):
  truncates to a byte cap (default 128 KiB; the agent loop passes 512 MiB), then redacts
  line-by-line against ~50 command-override
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

- Each session id is `<cwd-key>-<unix-millis>-<8 random hex>`; the cwd key is the first 16 hex characters of the
  SHA-256 of the normalized absolute working directory (lowercased on Windows).
- **Each session is a folder**: `.cell/sessions/<cwd-key>/<id>/`. It holds the JSONL transcript
  `messages.jsonl` (one message object per line — the standard agent log shape), the Teamwork store
  `teamworks.json` with one `<job-id>/` transcript directory per job, and the compaction archives
  `saved/msg-<UTC time>.jsonl`. `.cell/sessions/sessions.json` is the hash → path index that lets
  every group be resolved back to a real directory; `/sessions` groups by it and marks the current
  cwd with `>`.
- `/session ID` and startup resume logic **follow the session's cwd** (resolved through the index,
  `current_path` + cache invalidation), so tools keep operating on the project the conversation
  belongs to; `/session` and `/session rm ID` reset the read-before-edit log because recorded reads
  no longer apply. Switching to an existing session prints its last five user/assistant text
  messages again, omitting thinking and tool-call content.
- `/new` keeps the old folder (revisitable), `/clear` keeps the id, `/session rm` deletes the whole
  folder and the usage record; orphaned usage records are pruned at startup and on `/usages`.
- **Compaction archives**: every `/compact` (manual or automatic) first writes the complete
  transcript to `saved/msg-<UTC time>.jsonl` inside the session folder (same-second archives get a
  `-N` suffix) before the context is rewritten; if the archive or the summary cannot be produced
  the context is left untouched. `/saved list`, `/saved show NAME` and `/saved rm NAME` access the
  archives of the current session.
- Session, config and index writes go through `cell::async_io::file_writer` (coalesced per path, one
  background thread). Every write is an **atomic replace**: the content is written to a sibling
  `<name>.tmp`, flushed to the storage device, then renamed over the target, so a crash can never
  leave a truncated file that the next start silently reads as empty. Commands that read those files back (`/save`, `/sessions`, `/session`,
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
  "compact_auto": true,
  "compact_provider": "",
  "compact_model": "",
  "system": "You are a helpful assistant.…",
  "session": "6333a2b6f7084f1a-1787819024",
  "log_max_lines": 1000,
  "thread_pool_size": 16,
  "active_sessions": { "6333a2b6f7084f1a": "6333a2b6f7084f1a-1787819024" }
}
```

| Field | Default | Meaning |
| --- | --- | --- |
| `providers[]` | *(empty)* | One entry per endpoint: `name` (unique id), `style` (`openai` \| `anthropic`), `api_style` (`openai-chat` \| `openai-responses` \| `anthropic`), `base`, `key` (vault id, not the secret), `proxy`. Models are **not** stored here. |
| `current_provider` | first entry | Active provider when empty |
| `current_model` | — | Active model name; cleared automatically whenever the active provider actually changes (see [`/provide`](#slash-commands)) |
| `think_level` | `0` | Chain-of-thought level: 0=off, 1=low(1024), 2=med(2048), 3=high(4096), 4=max(8192). Legacy `think: true` maps to level 2. |
| `tools` | `true` | Tool calling enabled |
| `sandbox_mode` | `full-access` | See [sandbox modes](#1-sandbox-modes) |
| `autoallow` | `false` | LLM decides whether exec commands run (only effective in full-access) |
| `compact_auto` | `true` | Automatically compact the context after long tool-heavy turns (`/compact auto`) |
| `compact_provider` / `compact_model` | *(empty)* | Provider/model used for compaction summaries; empty means inherit the session model (`/compact model provider:model`, reset with `inherit`) |
| `teamwork_max_children` | `5` (clamped 1–100) | Maximum number of child agents per Teamwork job (`/teamworks max`) |
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
        └── 6333a2b6f7084f1a-1787819024-ab12cd34/   # one folder per session
            ├── messages.jsonl    # transcript: one message object per line
            ├── teamworks.json    # Teamwork store (jobs of this session)
            ├── tw-20260910-124236/          # one transcript dir per job
            │   └── worker_0.json
            ├── notices/
            │   ├── pending/      # undelivered notice files (nt-*.json)
            │   └── seen.jsonl    # audit trail of delivered/dropped notices
            └── saved/
                └── msg-20260910-124500.jsonl  # full transcript archived by /compact
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
cell::teamwork   Teamwork job store (cached in memory), child-agent scheduler (serial / pooled
                 parallel), reuse + history/report queries
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

**Failures and retries.** A failed LLM request is retried up to 5 attempts per round with a 5-second
delay between attempts (`[retrying in 5s... attempt N/5]`); exhausting them ends the turn with
`[llm error after 5 attempts]`. If the failure is a context-overflow error, the round is compacted
once and retried immediately instead of burning a retry attempt (see
[automatic compaction](#slash-commands)).

## Streaming UI and keyboard control

- `>` prompt, `reply>` prefix for streamed answers; the reasoning stream is printed **dim**, the
  answer **plain**, tool results and their echo **cyan**.
- While waiting for the first token a `⏳ Ns` spinner is refreshed every 500 ms; afterwards a
  `~N tok` counter is refreshed on line boundaries.
- **Esc** during a stream cancels it: the partial reply is kept in the transcript and `[cancelled]`
  is printed. (Implemented by `cell::plat::peek_key`, which temporarily puts stdin in raw mode.)
- **Ctrl+C** (and `SIGTERM`/`SIGHUP`/`SIGABRT` on Windows) runs the persistence hook — repair the tool
  transcript, save session, save config, flush async writes — restores the terminal and exits with the
  signal number. The terminating signals are taken from a dedicated `sigwait` thread, so the
  persistence code never runs inside an asynchronous signal handler. `SIGSEGV`/`SIGFPE`/`SIGILL`
  deliberately do **not** try to serialize state: they restore the default action and terminate.
- Non-interactive mode (stdin is not a tty) reads all of stdin as one message, requires a configured
  provider, runs the agent loop once and exits.

## Logging, signals and exit codes

Log lines are `[YYYY-MM-DD HH:MM:SS] LEVEL [cat  ] message` (UTC). Categories include `boot`, `core`,
`llm`, `tool`, `sess`, `ctx`, `cmd`, `user`, `probe`, `provider`, `model`, `think`, `sandbox`,
`skill`, `stats`, `vault`. Only `llm` and `tool` activity is mirrored to the console (`ERROR` always
is; `DEBUG` only with `--verbose`), so the terminal stays readable while the file keeps everything.
Writes are buffered in 16 KiB chunks and flushed immediately for `ERROR`.

| Situation | Exit code |
| --- | --- |
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
the tool registry and its policies, 16-way concurrent read-only tool calls, **Teamwork job
normalization (per-child provider/model/think, reuse references, name sanitizing, limits),
store round-trips, child append/remove, history and report queries**, incremental SSE parsing
and buffer compaction, the lazy directory walker, thread-pool job accounting, logger rotation, vault
round-trip and persistence, config save/load/migration/error handling, session grouping, `/new`
semantics, the cwd index, load-time re-sanitization of exec results, skill discovery (including
directory-style) and usage statistics. Prints `selftest OK` / `selftest FAILED`.

## Implementation notes

A few places where the code and its own help text/comments differ, or where behaviour is
intentionally simpler than it looks:

- The sandbox is **path-only**: `box::check_exec` / `box::check` do **not** parse command names,
  decode encoded payloads, or block network egress by binary name — they only reject path traversal
  and sensitive paths. Network egress is "denied" in the sense that there is no whitelist of allowed
  commands; `exec` is gated instead by the sandbox mode, a human confirmation (or autoallow), and the
  high-risk second-confirmation list.
- Switching providers resets the model name: `--provider NAME` (existing provider), `/provide NAME`
  and `/provide add` all route through `config::select_provider`, which clears `current_model` when
  the selection actually changes. Re-selecting the already-active provider keeps its model, and an
  empty `current_provider` counts as "first provider is active" for that purpose. A requested model
  on the same command line (`--model`) is applied after the switch.
- Startup resumes the **last session used in the current cwd**, recorded in `active_sessions`; if
  no record exists yet, the most recently written session file for the cwd is selected. The last
  five user/assistant text messages are printed again, excluding thinking and tool-call content.
  `--session` / the `session` field are still persisted for compatibility, but startup uses the
  active-session record instead.
- `exec` is the only tool whose output is scanned for injection fingerprints; the other tools are
  considered sandboxed at call time and get a size cap only. The tool descriptions themselves can
  also drift from the implementation (e.g. `exec`'s denies-network claim, `read`'s numbered lines),
  which is normal for strings embedded next to the code.
- The tool-output wrapper tag is written with an escaped forward slash in the source; the escape
  collapses to a plain slash at runtime, so the marker that reaches the transcript is an opening tag
  carrying the `tool` and `path` attributes, and the reload-time sanitizer keys off the `exec` value of
  that attribute.

## License

MIT — see [LICENSE](LICENSE).
