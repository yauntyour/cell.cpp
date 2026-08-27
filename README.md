<div align="center">

# cell.cpp

**A lightweight AI coding agent written in modern C++26**

Connect to OpenAI or Anthropic, chat with your codebase, and let the LLM read, write, edit, and execute — all sandboxed.

[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue?logo=cplusplus)](https://isocpp.org/)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

```
cell --provider openai --model gpt-4o --key sk-***
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
# Interactive mode
cell.exe --provider openai --model gpt-4o --key YOUR_API_KEY

# Use Anthropic
cell.exe --provider anthropic --model claude-sonnet-4-20250514 --key YOUR_API_KEY

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
| **edit** | 修改已有文件（通过 SEARCH/REPLACE 块，需确保搜索文本唯一） |
| **exec** | 执行 shell 命令（默认超时 30 秒，最大 300 秒） |

工具使用规则要点：
- 读取文件前必须先读取原内容；
- 使用 write 前需确保父目录已存在（可用 `exec` + `mkdir -p` 创建）；
- 单次 edit 不得涉及 3 个以上无关联的代码块；
- 未先读取文件内容时不得连续调用超过 3 次 rg；
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
  --provider openai|anthropic  llm provider (default: openai)
  --base URL                   api base url for the default model
  --model MODEL                default model name
  --proxy URL                  http(s) proxy for the default model
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
| `--provider` | `openai` \| `anthropic` | `openai` | LLM provider, selects the API protocol and endpoint style |
| `--base` | URL | see below | API base URL — works with the official APIs or any OpenAI-compatible gateway |
| `--model` | model name string | see below | Model name, e.g. `gpt-4o-mini`, `claude-3-5-haiku-latest` |
| `--proxy` | URL | none | HTTP(S) proxy for the current model, e.g. `http://user:pass@proxy:8080`; localhost/loopback traffic always bypasses it |
| `--key` | API key | none | Never stored in plaintext — encrypted with libsodium into `.cell/.crypt` |
| `--session` | session ID | last used session | Resumes chat history from `.cell/sessions/<ID>.json` |
| `--system` | prompt text | built-in coding-agent prompt | Replaces the system prompt entirely |
| `--no-color` | flag (no value) | off | Disables ANSI colors in log output |
| `--verbose` | flag (no value) | off | Also prints DEBUG-level logs to the console (DEBUG logs always go to the log file) |
| `--selftest` | flag (no value) | off | Runs the built-in test suite instead of entering the chat |

#### 1.2 Configuration Load & Override Order

At startup the final configuration is assembled as follows:

1. `.cell/config.json` is loaded (model list + current model index + system prompt + last session ID + log cap);
2. A fresh install ships with **no models** — on first launch cell prints a hint and you register a model
   with `/model openai:gpt-4o base:URL key:KEY` (inside the REPL) or via the CLI options below;
3. `--provider` / `--base` / `--model` / `--proxy` are applied **in place onto the current model entry** (only non-empty fields are changed);
4. `--key` is written to the encrypted vault and bound to the current entry; `--session` / `--system` directly replace the corresponding fields.

> Note: CLI overrides happen after the config is loaded, and the merged result is **written back** to
> `config.json` on exit — so provider/base/model changes passed on the command line are persistent
> and still apply on the next launch without arguments.

#### 1.3 Option Details

**`--provider` / `--base` / `--model` / `--proxy`**

All four modify the *current model entry* (the item `current_model` points to in `config.json`,
entry 0 by default). You don't need to pass all of them — for example, switching only the protocol:

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

If an entry's base is empty, OpenAI defaults to `https://api.openai.com/v1` and Anthropic defaults
to `https://api.anthropic.com` — give models you configure yourself an explicit base instead.

**`--key`**

The key is encrypted and stored in `.cell/.crypt` under the id `model:<provider>:<model>`
(e.g. `model:openai:gpt-4o-mini`) and bound to the current model entry. Passing a key again for the
same model overwrites the old value; the plaintext is securely zeroed in memory right after.

**API Key resolution priority** (decided per request)

1. The vault key bound to the current entry (written by `--key` or `/model ... key:KEY`);
2. The environment variable `OPENAI_API_KEY` (openai) or `ANTHROPIC_API_KEY` (anthropic);
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
| `/models` | List configured models (entry 0 is the default) |
| `/model [provider:NAME] [base:URL] [key:KEY] [proxy:URL]` | Switch models; with `base:`/`key:`/`proxy:` it registers/updates instead |
| `/model rm provider:NAME` | Delete a model (also removes its stored vault key) |
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

#### 2.2 `/models` — List Models

Example output:

```
  [0] openai:gpt-4o-mini  <current>
       base: https://api.openai.com/v1
       proxy: http://user:pass@proxy:8080
       key:  stored
  [1] anthropic:claude-3-5-haiku-latest
       base: https://api.anthropic.com
```

The number in brackets is the index (i.e. `current_model`); `<current>` marks the active model.
`key: stored` means a vault key is bound to that model — otherwise resolution falls back to the
environment variable or the generic `api_key`. A `proxy:` line means API traffic for that model is
routed through the given HTTP(S) proxy; without one, libcurl's default (environment) settings apply.

#### 2.3 `/model` — Switch / Register Models

**Form 1: switch to an existing model**

```
/model NAME                  # matched exactly as provider:NAME under the current provider
/model provider:NAME         # provider given explicitly
```

Matching rule: a bare `NAME` is combined with the current model's provider into a label and must
**match exactly** (e.g. when the current provider is openai, `/model gpt-4o` matches
`openai:gpt-4o`). If not found, an error suggests using the register form instead.

**Form 2: register a new model / update an existing one (and switch to it)**

Carrying `base:` or `key:` (either one) switches to register mode:

```
/model anthropic:claude-opus4.8 base:https://api.anthropic.com key:sk-ant-xxx
/model openai:gpt-4o base:https://your-proxy.example.com/v1
/model openai:gpt-4o proxy:http://user:pass@proxy:8080
```

**Form 3: delete a model**

```
/model rm provider:NAME        # e.g. /model rm openai:gpt-4o
```

The entry is removed from `config.json` and any vault key bound to it
(`model:<provider>:<name>`) is deleted too. If the removed entry was the current model, the
selection moves to the next remaining entry (or none).

Behavior details:

- If the label `provider:model` doesn't exist → a new entry is appended and made current;
- If it exists → only explicitly provided fields are updated (base changes only when `base:` is
  given; an empty `key:` does not clear the old secret; `proxy:` similarly only updates when given);
- `key:KEY` is stored encrypted in the vault under `model:<provider>:<model>`;
- The result is written back to `.cell/config.json` immediately — no `/save` needed.

**Tolerant spacing**: these forms are equivalent (values may also sit in the next token):

```
/model anthropic: claude-opus4.8 base: https://api.anthropic.com key: sk-ant-xxx
```

Unrecognized extra tokens produce a warning and are ignored. Note: plain `/model NAME` only
*switches* — nothing is created; registering requires `base:` or `key:`.

**Connectivity probe**: at startup, after `/new`, and after every `/model` change, cell performs a
lightweight GET on the model's endpoint (OpenAI: `GET {base}/models`; Anthropic:
`GET {base}/v1/models`; 5s timeout) and logs the result. A failure prints
`[model unreachable] <label>: <reason>` as a warning but does not block the REPL.

#### 2.4 `/sessions` — Session List

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

#### 2.5 `/usages` — Usage Statistics

Reads and summarizes `.cell/usages.json`, grouped by model and by session:

`requests`, `messages` (messages added), `in_chars`/`out_chars` (characters sent/received),
and `in_tok`/`out_tok` (tokens — present only if the API returns usage data).

#### 2.6 `/compact` — Context Compaction

Use this when a long conversation inflates the context. Strategy:

- The **first system prompt message is kept as-is**;
- **Every other message** (skills/system injections, user, assistant, tool results) is fed to the
  current model and aggregated into a **single summary message** of the form
  `{"role": "system", "content": "Here is a summary that ..."}`;
- With 12 or fewer messages to aggregate it reports "already small" and does nothing;
- If summarization fails, a placeholder truncation text is used as fallback;
- Session and config are saved immediately afterwards.

Compaction itself is one LLM call and counts toward usage statistics.

#### 2.7 `/skills` and `/skill` — Skill System

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

#### 2.8 `/clear`, `/save` and `/new`

- `/clear`: empties the current session's message history but **keeps the session id** — the
  system prompt and skill list are re-injected and the file is saved immediately. Useful for
  starting a long conversation over without losing the session's identity;
- `/save`: writes the current session to `.cell/sessions/<id>.json` right away. The session is also
  auto-saved after every conversation round, after `/compact` and after `/skill` — `/save` is mainly
  a manual safety net;
- `/new`: saves the current session, then starts a fresh session (ID = Unix timestamp in seconds)
  with the system prompt and skill list re-injected. **Old session files are kept on disk** and can
  be revisited with `/session ID`.

#### 2.9 Appendix: Tool Approval Prompts

During a conversation the model may call one of 8 tools:

| Tool | Purpose | Approval |
|---|---|---|
| `ls` | List a directory (one level, paginated, max 500 entries/page) | read-only, runs automatically |
| `read` | Read a file (max 1MB per call; optional line-range segmenting) | read-only, runs automatically |
| `rg` | Recursive content search (skips hidden files and `.gitignore` paths, max 500 matches) | read-only, runs automatically |
| `glob` | Find files by name pattern (e.g. `**/*.test.ts`) | read-only, runs automatically |
| `find` | Filter files by metadata (name, size, modification time) | read-only, runs automatically |
| `write` | Create a **new** file (refuses to overwrite; refuses if the parent directory is missing) | confirmation required |
| `edit` | Modify an existing file via a SEARCH/REPLACE block (ambiguous matches abort with all locations reported) | confirmation required |
| `exec` | Run build/test/git commands (default 30s timeout; result ends with `exitcode=N`) | confirmation required |

Read-only tools run without asking and may execute **concurrently** within one round; `write`,
`edit` and `exec` run sequentially and show an interactive prompt before every execution:

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
cell::box      ─ Sandboxed system operations (file I/O, exec, rg, glob, find, SEARCH/REPLACE edit)
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
├── config.json       # Multi-model config: {"models":[{provider,base,model,key,proxy}...], "current_model":N, "log_max_lines":1000}
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

## License

MIT
