# llm-chat-cpp — MCP-Powered Terminal Chat

A C++ terminal chat application that connects to llama.cpp and an MCP server for tool-assisted conversations.

## Requirements

- C++17 compiler (g++ ≥ 9, clang ≥ 10, or MSVC 2019+)
- CMake ≥ 3.16
- libcurl (development headers)
- GNU Readline (development headers, Linux only)
- nlohmann/json (development headers)
- [vcpkg](https://github.com/microsoft/vcpkg) (Windows only)

### Ubuntu/Debian

```bash
sudo apt install build-essential cmake libcurl4-openssl-dev libreadline-dev nlohmann-json3-dev
```

### Arch Linux

```bash
sudo pacman -S base-devel cmake curl readline nlohmann-json
```

### Fedora

```bash
sudo dnf install gcc-c++ cmake libcurl-devel readline-devel nlohmann-json-devel
```

## Build

### Linux

```bash
cmake -B build
cmake --build build
```

Binary: `build/llm-chat`

### Windows (MSVC + vcpkg)

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

Binary: `build\Release\llm-chat.exe`

> Dependencies (`curl`, `nlohmann-json`) are installed automatically via vcpkg manifest (`vcpkg.json`).

## Usage

### 1. Start llama.cpp

```bash
llama-server -m <model.gguf> --port 8080
```

### 2. Start MCP server

```bash
# example with server.py
uv run server.py
```

### 3. Run llm-chat

```bash
./build/llm-chat
```

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `LLM_CHAT_LLM_URL` | `http://localhost:8080` | llama.cpp server address |
| `LLM_CHAT_MCP_URL` | `http://localhost:8000/mcp` | MCP server address |
| `LLM_CHAT_MAX_CONTEXT` | `8192` | Max context token count (for percentage display) |

```bash
export LLM_CHAT_LLM_URL="http://10.0.0.1:8080"
export LLM_CHAT_MCP_URL="http://10.0.0.1:8000/mcp"
export LLM_CHAT_MAX_CONTEXT=4096
./build/llm-chat
```

> **Note:** If the llama.cpp server provides `n_ctx` (or `max_context_length`) in the `/v1/models` endpoint, it overrides `LLM_CHAT_MAX_CONTEXT`.

## Commands

| Command | Description |
|---|---|
| `/quit` or `/exit` | Exit |
| `/help` | Show command list |
| `/clear` | Clear chat history |
| `/tools` | List MCP tools |
| `/read` | Add file to context |
| `/files` | List attached files |
| `/remove` | Remove attached file |
| `/clearfiles` | Remove all attached files |
| `/models` | List / switch / unload models |


## Architecture

```
┌─────────────┐      ┌──────────────┐      ┌──────────────┐
│  llm-chat   │─────▶│  llama.cpp   │      │  MCP Server  │
│  (C++ CLI)  │      │  :8080       │      │  :8000/mcp   │
│             │      │  /v1/chat/   │      │  JSON-RPC    │
│             │      │  completions │      │  tools/list  │
│             │      └──────────────┘      │  tools/call  │
│             │                           └──────────────┘
└─────────────┘
```

1. User message → llama.cpp API (with tool definitions)
2. If model returns `tool_calls` → execute via MCP
3. Tool result sent back to model
4. Model final response displayed to user
5. Stats shown after each response (tokens, time, t/s, context %)
