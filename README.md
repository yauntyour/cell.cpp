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

1. `.cell/config.json` is loaded (model list + current model index + system prompt + last session ID);
2. If no models are configured, two defaults are generated:
   - `[0] openai:gpt-4o-mini` (base: `https://api.openai.com/v1`)
   - `[1] anthropic:claude-3-5-haiku-latest` (base: `https://api.anthropic.com`)
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
to `https://api.anthropic.com` (pre-filled only in the auto-generated default entries; models you
configure yourself should get an explicit base).

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
| `/sessions` | List saved sessions |
| `/session ID` | Switch to another saved session |
| `/usages` | Show per-model and per-session usage statistics |
| `/compact` | Compress the current session context |
| `/skills` | List available skills |
| `/skill NAME` | Load a skill into the current session |
| `/save` | Save the current session immediately |
| `/new` | Discard the current session and start a fresh one |
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

#### 2.4 `/sessions` — Session List

Scans `.cell/sessions/*.json` and prints one line per session: session ID, message count, and a
snippet of the first user message (≤60 chars); `*` marks the current session:

```
  1756160000 *  messages=42  "migrate this project to C++20"
  1756089312    messages=7   "fix the curl timeout issue"
```

`/session ID` loads another saved session (the current one is saved first); an ID with no file on
disk simply starts a fresh session — same semantics as `--session`.

#### 2.5 `/usages` — Usage Statistics

Reads and summarizes `.cell/usages.json`, grouped by model and by session:

`requests`, `messages` (messages added), `in_chars`/`out_chars` (characters sent/received),
and `in_tok`/`out_tok` (tokens — present only if the API returns usage data).

#### 2.6 `/compact` — Context Compaction

Use this when a long conversation inflates the context. Strategy:

- All system messages are kept;
- Of the non-system messages, the **first 2 and last 6** are kept;
- The middle messages are summarized by the current model (streamed live with the prefix
  `summary> `), and the summary is inserted as a new system message;
- With 12 or fewer non-system messages it reports "already small" and does nothing;
- If summarization fails, a placeholder truncation text is used as fallback;
- Kept history messages are rewritten as plain text to avoid dangling tool_call ids;
- Session and config are saved immediately afterwards.

Compaction itself is one LLM call and counts toward usage statistics.

#### 2.7 `/skills` and `/skill` — Skill System

Skills are Markdown files in `.cell/skills/*.md`. They are discovered only if they start with a
YAML-style front matter block (otherwise ignored):

```markdown
---
name: build-helper
description: helpers for building cell
---
Body instructions injected when the skill is loaded...
```

- `name` falls back to the file name (without extension); `description` falls back to the first
  non-empty body line (truncated at ~120 characters);
- As long as valid skills exist, every **new session** gets an "available skills" list injected as a
  system message, so the model can suggest loading a matching skill;
- `/skill NAME` wraps the skill body into a system message appended to the current session and saves
  immediately; it stays in effect for the rest of the session (loading repeatedly appends copies).

#### 2.8 `/save` and `/new`

- `/save`: writes the current session to `.cell/sessions/<id>.json` right away. The session is also
  auto-saved after every conversation round, after `/compact` and after `/skill` — `/save` is mainly
  a manual safety net;
- `/new`: **deletes** the current session file, generates a fresh session (ID = Unix timestamp in
  seconds), and re-injects the system prompt and skill list. The old conversation cannot be
  recovered — use with care.

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
├── config.json       # Multi-model config: {"models":[{provider,base,model,key,proxy}...], "current_model":N}
├── .crypt            # Encrypted API key vault
├── .key              # Symmetric encryption key
├── usages.json       # Per-model and per-session usage statistics
├── skills/
│   └── *.md          # Skills (front matter: name/description, body injected on /skill)
├── logs/
│   └── cell.log      # Timestamped application logs
└── sessions/
    ├── <id>.json     # Persisted chat sessions
    └── ...
```

## License

MIT
