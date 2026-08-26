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
| 🔧 | **8 Built-in Tools** | `exec`, `read`, `write`, `edit`, `grep`, `exist`, `remove`, `mkdir` |
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
g++ -std=c++26 -O2 -o cell.exe cell.cpp \
    -lcurl -lsodium -lws2_32 \
    -I<vcpkg>/installed/x64-windows/include \
    -L<vcpkg>/installed/x64-windows/lib
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

```
usage: cell [options]
  --provider openai|anthropic  llm provider (default: openai)
  --base URL                   api base url
  --model MODEL                model name
  --key KEY                    api key (saved to the encrypted vault)
  --session ID                 resume an existing session
  --system TEXT                system prompt
  --no-color                   disable colored log output
  --selftest                   run internal self tests
```

### Slash Commands

| Command | Description |
|---------|-------------|
| `/exit` | Exit the REPL |
| `/save` | Save current session to disk |
| `/new`  | Start a fresh session |

## Architecture

The entire application lives in a single `cell.cpp` file, organized into clean namespaces:

```
cell::box      ─ Sandboxed system operations (file I/O, exec, edit, grep)
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
├── config.json       # Provider, model, system prompt, session ID
├── .crypt            # Encrypted API key vault
├── .key              # Symmetric encryption key
├── logs/
│   └── cell.log      # Timestamped application logs
└── sessions/
    ├── <id>.json     # Persisted chat sessions
    └── ...
```

## License

MIT
