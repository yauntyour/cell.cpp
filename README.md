<div align="center">

# cell.cpp

**A lightweight AI coding agent written in modern C++26**

Connect to OpenAI or Anthropic, chat with your codebase, and let the LLM read, write, edit, and execute — all sandboxed.

[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue?logo=cplusplus)](https://isocpp.org/)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

```
cell --provider openai --base https://api.openai.com/v1 --model gpt-4o --key sk-***
```

</div>

---

## Features

| | Feature | Description |
|---|---|---|
| 🤖 | **Dual LLM Support** | OpenAI and Anthropic APIs, streaming & non-streaming |
| 🔧 | **8 Built-in Tools** | `ls`, `read`, `write`, `edit`, `rg`, `exec`, `glob`, `find` |
| 🔒 | **Encrypted Vault** | libsodium-based credential storage with secure memory handling |
| 💬 | **Session Persistence** | Chat history saved and restored across sessions |
| 🛡️ | **Security Sandbox** | Blocks dangerous commands (`rm -rf`, `shutdown`, path traversal...) |
| 🎨 | **Colored Output** | ANSI escape codes with Windows Virtual Terminal support |
| ⚡ | **Agent Loop** | Up to 8 rounds of tool calls per user message |
| 🧠 | **Chain of Thought** | `/think` streams reasoning output (OpenAI `reasoning_content` / Anthropic `thinking`) |
| 🧪 | **Self-Test** | Built-in test suite — run with `--selftest` |

## Quick Start

### Prerequisites

- **Compiler:** GCC (MSYS2 UCRT64) or any C++26-capable compiler
- **Libraries:** libcurl, nlohmann/json, libsodium
- **Package manager (recommended):** [vcpkg](https://github.com/microsoft/vcpkg)

Install dependencies via vcpkg:

```bash
vcpkg install curl nlohmann-json libsodium:x64-windows
```

### Build

```bash
g++ -std=c++26 -O2 -o cell.exe cell.cpp -lcurl -lsodium
```

### Run

```bash
# Interactive mode (creates an openai provider + stores the key in the encrypted vault)
cell.exe --provider openai --base https://api.openai.com/v1 --model gpt-4o --key YOUR_API_KEY

# Use Anthropic
cell.exe --provider anthropic --base https://api.anthropic.com --model claude-sonnet-4-20250514 --key YOUR_API_KEY

# Inside the REPL, providers are managed with /provide:
#   /provide add openai:https://api.openai.com/v1 key:YOUR_API_KEY   add + select a provider
#   /provides                                                        list providers
#   /models                                                          fetch the model list
#   /model gpt-4o                                                    switch model
#   /think                                                           toggle chain-of-thought

# Resume a saved session
cell.exe --provider openai --model gpt-4o --session my-session

# Run built-in tests
cell.exe --selftest
```

### Example

```bash
cell: session=6333a2b6f7084f1a-1787819024 model=openai:Qwen3.8-27B
context: loaded 7 message(s) from disk
> /sessions
  > cwd: C:\Users\yaunt
    6333a2b6f7084f1a-1787819024 *  messages=7  "你好，请介绍一下你自己"
> 还记得前面聊了什么吗
记得呀，咱们前面聊了这些内容：

1. **你让我介绍自己** —— 我介绍了我是千问（Qwen），由阿里巴巴研发的大语言模型，具备长文理解、逻辑推理、多任务能力等特性。

2. **你让我查看当前目录下的项目** —— 我列出了目录内容，发现这是一个 Windows 用户主目录（包含 `Desktop`、`Documents`、`source`、各种开发工具配置目录如 `.cargo`、`.pnpm`、`.gradle` 等），而不是某个具体的项目根目录。我还提到了一些零散文件，比如一个约 790MB 的 `java_error_in_studio.hprof` 崩溃转储文件。

我最后还问你是否需要进一步查看某个具体目录（比如 `source` 或 `Documents`）。

接下来想继续做什么呢？😊
[2026-08-27 08:31:55] INFO  [llm  ] round=1 model=openai:Qwen3.8-27B stream=true ctx_msgs=8 tok_in=3424 tok_out=393 cache=92.6% time=19.95s ttf=11.06s tools=0
> 检查工具列表
以下是我当前拥有的工具列表：

| 工具 | 说明 |
|------|------|
| **ls** | 列出目录内容（单级，分页，每页最多 500 条） |
| **read** | 读取文件内容（单次最大 1MB，支持按行号分段读取） |
| **rg** | 递归搜索文件内容（跳过隐藏文件和 .gitignore 路径） |
| **glob** | 按文件名模式查找文件（如 `**/*.ts`） |
| **find** | 按元数据筛选文件（名称、修改时间、大小等） |
| **write** | 创建新文件（不能覆盖已存在的文件） |
| **edit** | 修改已有文件（多模式：replace 整段/精准替换、insert 插入、append 追加、delete 删除、query 定位查询；编辑前必须先 read 过该文件/行） |
| **exec** | 执行 shell 命令（默认超时 30 秒，最大 300 秒） |

工具使用规则要点：
- edit 前必须先读取原内容：edit 只允许修改 read 工具已返回过的文件（或行范围），未读过的文件/行会被拒绝；edit 的 `query` 模式为只读定位，不受此限制；
- edit 支持 replace / insert / append / delete / query 五种模式：replace 用小段唯一文本做精准替换、多行块做整段替换，insert 在唯一文本（或指定行）之后插入，append 在文件末尾追加，delete 删除唯一文本或行区间，query 定位所有匹配并报告行号与上下文；
- 使用 write 前需确保父目录已存在（可用 `exec` + `mkdir -p` 创建）；
- `exec` 后需根据返回值判断成功与否。

需要我用这些工具做什么？比如继续查看刚才目录里的某个项目？
[2026-08-27 08:32:52] INFO  [llm  ] round=1 model=openai:Qwen3.8-27B stream=true ctx_msgs=10 tok_in=3625 tok_out=417 cache=94.3% time=21.24s ttf=5.53s tools=0
> /compact
context already small (10 messages to aggregate)
> /clear
context: injected system_prompt=1740chars
session cleared: 6333a2b6f7084f1a-1787819024 (removed 11 messages)
> /quit
```

## Usage

### 1. Command-Line Options

```
usage: cell [options]
  --provider NAME              select an existing provider, or create one (style = NAME)
  --base URL                   api base url for the current provider
  --model MODEL                default model name
  --proxy URL                  http(s) proxy for the current provider
  --key KEY                    api key (saved to the encrypted vault)
  --session ID                 resume an existing session
  --system TEXT                system prompt
  --no-color                   disable colored log output
  --verbose                    enable DEBUG-level log output on console
  --selftest                   run internal self tests
```

#### 1.1 Option Reference

| Option | Value | Default | Description |
|---|---|---|---|
| `--provider` | provider name (`openai` \| `anthropic` \| any alias) | none | Selects an existing provider; if it does not exist, one is created (the value doubles as the API style) |
| `--base` | URL | see below | API base URL — works with the official APIs or any OpenAI/Anthropic-compatible gateway |
| `--model` | model name string | none | Model name, e.g. `gpt-4o-mini`, `claude-3-5-haiku-latest` |
| `--proxy` | URL | none | HTTP(S) proxy for the current provider, e.g. `http://user:pass@proxy:8080`; localhost/loopback traffic always bypasses it |
| `--key` | API key | none | Never stored in plaintext — encrypted with libsodium into `.cell/.crypt` |
| `--session` | session ID | last used session | Resumes chat history from `.cell/sessions/<ID>.json` |
| `--system` | prompt text | built-in coding-agent prompt | Replaces the system prompt entirely |
| `--no-color` | flag (no value) | off | Disables ANSI colors in log output |
| `--verbose` | flag (no value) | off | Also prints DEBUG-level logs to the console (DEBUG logs always go to the log file) |
| `--selftest` | flag (no value) | off | Runs the built-in test suite instead of entering the chat |

#### 1.2 Configuration Load & Override Order

At startup the final configuration is assembled as follows:

1. `.cell/config.json` is loaded (provider list + current provider/model + system prompt + last session ID + log cap);
2. A fresh install ships with **no providers and no models** — on first launch cell prints a hint and
   you register a provider with `/provide add openai:URL key:KEY` (inside the REPL) or via the CLI
   options below;
3. `--provider` selects an existing provider by name, or (legacy behavior) creates one from
   `--base` / `--model` / `--proxy`; non-empty fields are applied in place onto the current provider;
4. `--key` is written to the encrypted vault and bound to the current provider; `--session` /
   `--system` directly replace the corresponding fields.

> Note: CLI overrides happen after the config is loaded, and the merged result is **written back** to
> `config.json` on exit — so provider/base/model changes passed on the command line are persistent
> and still apply on the next launch without arguments.

#### 1.3 Option Details

**`--provider` / `--base` / `--model` / `--proxy`**

These modify the *current provider entry*. `--provider` selects an existing provider by name; when
the name does not exist it is created and its name doubles as the API style (`openai` or
`anthropic`). You don't need to pass all of them — for example, switching only the style:

```bash
cell.exe --provider anthropic          # keeps current base/model, changes protocol only
```

Use `--base` to target third-party OpenAI-compatible services:

```bash
# local Ollama / vLLM / One-API etc.
cell.exe --base http://localhost:11434/v1 --model qwen2.5-coder
```

Use `--proxy` to route API traffic through an HTTP(S) proxy (credentials may be embedded in the
URL); `localhost`, `127.0.0.1` and `::1` are always excluded:

```bash
cell.exe --proxy http://user:pass@proxy.example.com:8080
```

If a provider's base is empty, OpenAI defaults to `https://api.openai.com/v1` and Anthropic defaults
to `https://api.anthropic.com` — give providers you configure yourself an explicit base instead.

**`--key`**

The key is encrypted and stored in `.cell/.crypt` under the id `provider:<name>`
(e.g. `provider:openai`) and bound to the current provider entry. Passing a key again for the
same provider overwrites the old value; the plaintext is securely zeroed in memory right after.

**API Key resolution priority** (decided per request)

1. The vault key bound to the current provider (written by `--key` or `/provide add ... key:KEY`);
2. The environment variable `OPENAI_API_KEY` (openai style) or `ANTHROPIC_API_KEY` (anthropic style);
3. The generic vault key `api_key`.

If none of the three is available, the first turn fails with an error explaining how to set one.

**`--session`**

Loads `.cell/sessions/<ID>.json`; a missing file simply starts a fresh session. Because the session
ID is written back to `config.json` on exit, **the next launch automatically resumes the previous
conversation**. To force a new one, pass any new `--session <ID>` or run `/new` inside the REPL.

**`--system`**

Fully replaces the default system prompt (which instructs the model to act as a coding agent that
uses tools and replies with a short summary when done). Quote it if it contains spaces:

```bash
cell.exe --system "You are a code assistant that answers in English only"
```

**`--no-color` / `--verbose`**

- Logs are always appended to `.cell/logs/cell.log`;
- By default the console shows INFO/WARN/ERROR only; with `--verbose`, DEBUG logs (cache hits,
  prompt injection, session persistence, ...) are printed too;
- `--no-color` disables ANSI colors (on Windows this also depends on virtual terminal support).

**`--selftest`**

Runs unit tests for the sandbox, editor, crypto, config, skills and stats subsystems inside an
isolated `.cell-selftest/` directory, which is cleaned up afterwards. Handy for a quick check after
building; exit code `0` means everything passed.

#### 1.4 Error Handling & Exit Codes

| Case | Behavior | Exit code |
|---|---|---|
| Normal exit / self-test passed | — | `0` |
| Unknown option | Error message + usage text | `1` |
| Missing option value (e.g. bare `--model`) | `missing value for --model` | `1` |
| Fatal error at runtime | Logged and printed as `fatal: ...` | `1` |

#### 1.5 Non-Interactive (Pipe) Mode

When stdin is not a terminal (pipe or redirection), the program treats **all of stdin as a single
message**, prints the reply, and exits automatically — useful in scripts:

```bash
echo "explain what this code does" | cell.exe
cell.exe < review-request.txt
git diff | cell.exe        # chain with other commands in a pipeline
```

### 2. Slash Commands

Inside the REPL, input starting with `/` is parsed as a command (split on whitespace) and is never
sent to the model.

#### 2.1 Command Overview

| Command | Description |
|---------|-------------|
| `/help` | Show all commands |
| `/provides` | List configured providers |
| `/provide NAME` | Select a provider (persists across sessions) |
| `/provide add openai:URL [key:KEY] [proxy:URL] [name:ALIAS]` | Add a provider (openai or anthropic API style) |
| `/provide rm NAME` | Delete a provider (also removes its stored vault key) |
| `/models` | Fetch the model list from the current provider |
| `/model NAME` | Switch to a model of the current provider |
| `/think [on\|off]` | Toggle chain-of-thought (CoT) output |
| `/tool [on\|off]` | Toggle tool calls (off = plain chat, no tools sent to the model) |
| `/sessions` | List saved sessions |
| `/session ID` | Switch to another saved session |
| `/usages` | Show per-model and per-session usage statistics |
| `/compact` | Compress the current session context |
| `/skills` | List available skills |
| `/skill NAME` | Load a skill into the current session |
| `/save` | Save the current session immediately |
| `/clear` | Clear the current session context (keeps the session id) |
| `/new` | Start a fresh session (old sessions are kept on disk) |
| `/exit` \| `/quit` | Exit the program |

#### 2.2 `/provides` and `/provide` — Providers

A **provider** is one API endpoint in one API style. The style prefix selects the protocol and
endpoint format (`openai` → `{base}/chat/completions`, `anthropic` → `{base}/v1/messages`).
Models are **not** stored in the config — they are fetched from the provider's models endpoint
(`GET {base}/models` / `GET {base}/v1/models`); only the active model name is persisted.

```
/provides                              # list providers
/provide NAME                          # select a provider (persists across sessions)
/provide add openai:URL [key:KEY] [proxy:URL] [name:ALIAS]
/provide rm NAME
```

`/provides` example output:

```
  [0] openai  <current>
       style: openai
       base:  https://api.openai.com/v1
       key:   stored
       model: gpt-4o-mini
  [1] claude
       style: anthropic
       base:  https://api.anthropic.com
```

- Adding a provider: the prefix (`openai:` / `anthropic:`) selects the API style, the rest of the
  token is the base URL (a spaced form `openai: URL` is accepted too). The provider is named after
  the style unless `name:ALIAS` is given, and it becomes the current provider immediately;
- `key:KEY` is stored encrypted in the vault under `provider:<name>`; without it, resolution falls
  back to the environment variable or the generic `api_key`;
- `proxy:URL` routes that provider's API traffic through the given HTTP(S) proxy;
- `/provide rm NAME` removes the provider and its vault key; if it was the current provider, the
  selection moves to the first remaining one (and the model name is cleared, since it belonged to
  the removed provider);
- Selecting a provider is written back to `.cell/config.json` immediately, so it survives restarts.

#### 2.3 `/models` and `/model` — Models

`/models` fetches the model list from the current provider's models endpoint and prints it with
`<current>` marking the active model:

```
provider openai (openai) - current model: gpt-4o-mini
  gpt-4o-mini  <current>
  gpt-4o
  gpt-4.1-mini
```

A failed fetch (network error, wrong key, HTTP error) prints the reason; the REPL keeps running.

`/model NAME` switches the current model by name and persists it:

```
/model gpt-4o              # switch to gpt-4o under the current provider
```

The name is stored as-is (it is not required to appear in the fetched list — a warning is printed
when it does not, since some gateways accept arbitrary names). There is no registration/delete
form: adding a provider is done with `/provide add`, and deleting one with `/provide rm`.

**Connectivity probe**: at startup, after `/new`, after `/provide add`, and after every `/model`
change, cell performs a lightweight GET on the provider's models endpoint (OpenAI:
`GET {base}/models`; Anthropic: `GET {base}/v1/models`; 5s timeout) and logs the result. A failure
prints `[provider unreachable] <name>: <reason>` as a warning but does not block the REPL.

#### 2.4 `/think` — Chain of Thought

`/think` toggles CoT output; `/think on` and `/think off` set it explicitly. The flag is persisted
in `config.json`.

- **OpenAI-style providers**: reasoning models already stream `delta.reasoning_content`; with
  `/think` on it is displayed dim/gray ahead of the answer (no extra request parameters are sent,
  so non-reasoning models are unaffected);
- **Anthropic-style providers**: the request body gains `thinking: {type:"enabled",
  budget_tokens:2048}` (max_tokens is raised accordingly) and the streamed `thinking_delta` blocks
  are displayed dim/gray. The thinking blocks are kept in the stored assistant message, which
  Anthropic requires when tool calls follow a thinking turn.

The reasoning text is display-only — it is not fed back to the model on later turns (except for
Anthropic's thinking blocks, which must be echoed verbatim).

#### 2.5 `/tool` — Tool Calls

`/tool off` disables tool calling: the tool definitions are no longer sent with the request, so
the model answers as a plain chat without touching the filesystem; `/tool on` re-enables them
(the flag is persisted in `config.json`). `/tool` alone toggles the state. The current state is
shown on the startup line (`tools=off` when disabled).

#### 2.6 `/sessions` — Session List

Scans `.cell/sessions/*.json` and prints one line per session: session ID, message count, and a
snippet of the first user message (≤60 chars); `*` marks the current session:

```
  1756160000 *  messages=42  "migrate this project to C++20"
  1756089312    messages=7   "fix the curl timeout issue"
```

`/session ID` loads another saved session (the current one is saved first); an ID with no file on
disk simply starts a fresh session — same semantics as `--session`.

`/session rm ID` deletes a session: the file `.cell/sessions/<id>.json` **and its usage record**
in `usages.json` are both removed (per-model aggregates are kept). Deleting the current session
switches to a fresh one. Orphaned usage records (session files deleted externally) are pruned
automatically at startup and when `/usages` runs.

#### 2.7 `/usages` — Usage Statistics

Reads and summarizes `.cell/usages.json`, grouped by model and by session:

`requests`, `messages` (messages added), `in_chars`/`out_chars` (characters sent/received),
and `in_tok`/`out_tok` (tokens — present only if the API returns usage data).

#### 2.8 `/compact` — Context Compaction

Use this when a long conversation inflates the context. Strategy:

- The **first system prompt message is kept as-is**;
- **Every other message** (skills/system injections, user, assistant, tool results) is fed to the
  current model and aggregated into a **single summary message** of the form
  `{"role": "system", "content": "Here is a summary that ..."}`;
- With 12 or fewer messages to aggregate it reports "already small" and does nothing;
- If summarization fails, a placeholder truncation text is used as fallback;
- Session and config are saved immediately afterwards.

Compaction itself is one LLM call and counts toward usage statistics.

#### 2.9 `/skills` and `/skill` — Skill System

Skills are Markdown files under `.cell/skills/`, scanned **recursively** (so directory-style
skills work too — e.g. `.cell/skills/my-suite/SKILL.md`). They are discovered only if they start
with a YAML-style front matter block (otherwise ignored):

```markdown
---
name: build-helper
description: helpers for building cell
---
Body instructions injected when the skill is loaded...
```

- `name` falls back to the file name (without extension); for directory-style skills named
  `SKILL.md`/`README.md` it falls back to the parent folder name; `description` falls back to the
  first non-empty body line (truncated at ~120 characters); surrounding quotes in front-matter
  values are stripped;
- As long as valid skills exist, every **new session** gets an "available skills" list injected as a
  system message, so the model can suggest loading a matching skill;
- `/skill NAME` wraps the skill body into a system message appended to the current session and saves
  immediately; it stays in effect for the rest of the session (loading repeatedly appends copies).

#### 2.10 `/clear`, `/save` and `/new`

- `/clear`: empties the current session's message history but **keeps the session id** — the
  system prompt and skill list are re-injected and the file is saved immediately. Useful for
  starting a long conversation over without losing the session's identity;
- `/save`: writes the current session to `.cell/sessions/<id>.json` right away. The session is also
  auto-saved after every conversation round, after `/compact` and after `/skill` — `/save` is mainly
  a manual safety net;
- `/new`: saves the current session, then starts a fresh session (ID = Unix timestamp in seconds)
  with the system prompt and skill list re-injected. **Old session files are kept on disk** and can
  be revisited with `/session ID`.

#### 2.11 Appendix: Tool Approval Prompts

During a conversation the model may call one of 8 tools:

| Tool | Purpose | Approval |
|---|---|---|
| `ls` | List a directory (one level, paginated, max 500 entries/page) | read-only, runs automatically |
| `read` | Read a file (max 1MB per call; optional line-range segmenting) | read-only, runs automatically |
| `rg` | Recursive content search (skips hidden files and `.gitignore` paths, max 500 matches) | read-only, runs automatically |
| `glob` | Find files by name pattern (e.g. `**/*.test.ts`) | read-only, runs automatically |
| `find` | Filter files by metadata (name, size, modification time) | read-only, runs automatically |
| `write` | Create a **new** file (refuses to overwrite; refuses if the parent directory is missing) | runs automatically (path sandbox-checked) |
| `edit` | Modify an existing file in 5 modes: `replace` (unique SEARCH block → replacement; short snippet for precise tweaks, multi-line block for whole rewrites), `insert` (after a unique text or a line), `append` (end of file), `delete` (unique text or a line range), `query` (read-only locate with line numbers). Ambiguous searches abort with every match reported. **Requires a prior read**: edits may only touch files/lines returned by an earlier `read` call, otherwise refused | runs automatically (path sandbox-checked) |
| `exec` | Run build/test/git commands (default 30s timeout; result ends with `exitcode=N`) | confirmation required |

Read-only tools run without asking and may execute **concurrently** within one round; `write` and `edit` also run without asking but are executed sequentially (after the concurrent pass, in call order) so edits to the same file never race and same-message reads always complete first. Only `exec` shows an interactive prompt before every execution:

```
allow exec({"cmd":"g++ -std=c++26 -O2 -o cell.exe cell.cpp"})? [y/N]
```

Answer `y` or `Y` to allow; anything else (including plain Enter) rejects that call.
High-risk commands (`rm -rf`, `chmod`, `chown`, `del /s`, ...) pass the sandbox but demand a
**second** confirmation. Before execution, every tool's arguments also pass a sandbox check:
paths containing `..`, or anything matching the dangerous command deny-list (`format`, `shutdown`,
`taskkill`, ...) are rejected outright.

## Architecture

The entire application lives in a single `cell.cpp` file, organized into clean namespaces:

```
cell::box      ─ Sandboxed system operations (file I/O, exec, rg, glob, find, multi-mode edit)
cell::net      ─ HTTP networking via libcurl (GET, POST, SSE streaming)
cell::sys      ─ Console output, logger, exceptions
cell::config   ─ Configuration persistence (.cell/config.json)
cell::encrypt  ─ Encrypted credential vault (libsodium)
cell::tools    ─ Tool abstraction & permission system
cell::llm      ─ OpenAI & Anthropic API clients
cell::chat     ─ Session management & message history
```

## How It Works

```
User Input ──▶ LLM API ──▶ Tool Call? ──▶ Execute in Sandbox ──▶ Feed Result ──▶ LLM API
                 ▲                                                  │
                 └──────────────────────────────────────────────────┘
                                    (up to 8 rounds)
```

1. You type a message
2. It's sent to the LLM (OpenAI or Anthropic)
3. If the LLM wants to use a tool, it returns a tool call
4. The tool executes in a sandboxed environment
5. The result is fed back to the LLM
6. This loops until the LLM produces a final response or the round limit is reached

## Runtime Data

The tool creates a `.cell/` directory in your working directory:

```
.cell/
├── config.json       # Provider config: {"providers":[{name,style,base,key,proxy}...], "current_provider":NAME, "current_model":NAME, "think":false, "tools":true, "log_max_lines":1000}
├── .crypt            # Encrypted API key vault
├── .key              # Symmetric encryption key
├── usages.json       # Per-model and per-session usage statistics
├── skills/
│   └── *.md          # Skills (front matter: name/description, body injected on /skill)
├── logs/
│   └── cell.log      # Timestamped application logs, trimmed to the last log_max_lines lines at startup
└── sessions/
    ├── <id>.json     # Persisted chat sessions
    └── ...
```

`log_max_lines` (default `1000`, minimum 10) controls how many lines `logs/cell.log` keeps — on
every startup the file is trimmed to its tail before new entries are appended.

`thread_pool_size` (default `16`, range 1–16) caps how many worker threads cell may spawn for
concurrent read-only tool calls (ls/read/rg/glob/find). The pool scales up dynamically under load,
so small values keep the machine quiet while larger ones speed up batched exploration.

## License

MIT
