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

### 2. Configure MCP Server

Create a `mcp.json` file in the same directory as the binary (or working directory):

```json
{
  "mcpServers": {
    "exa-search": {
      "type": "http",
      "url": "https://mcp.exa.ai/mcp"
    },
    "local-tools": {
      "type": "http",
      "url": "http://localhost:8000/mcp"
    }
  }
}
```

### 3. Run llm-chat

```bash
./build/llm-chat
```

If MCP connection fails at startup, the app continues without tools. Use `/mcp connect` later to retry.

## Command Line Options

| Option | Description |
|---|---|
| `--llm-url URL` | llama.cpp server address |
| `--mcp-url URL` | MCP server address (overrides mcp.json) |
| `--mcp-config PATH` | Path to mcp.json configuration file |
| `--help` | Show usage |

```bash
./build/llm-chat --llm-url http://10.0.0.1:8080 --mcp-config ./mcp.json
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
| `/mcp` | Show MCP connection status |
| `/mcp connect` | Connect to MCP server |
| `/mcp disconnect` | Disconnect from MCP server |
| `/read` | Add file to context |
| `/files` | List attached files |
| `/remove` | Remove attached file |
| `/clearfiles` | Remove all attached files |
| `/models` | List / switch / unload models |

## Architecture

```
┌─────────────┐      ┌──────────────┐      ┌──────────────┐
│  llm-chat   │─────▶│  llama.cpp   │      │  MCP Server  │
│  (C++ CLI)  │      │  :8080       │      │  (HTTP)      │
│             │      │  /v1/chat/   │      │  JSON-RPC    │
│             │      │  completions │      │  tools/list  │
│             │      └──────────────┘      │  tools/call  │
│             │                           └──────────────┘
│             │ mcp.json                   Supports SSE
│             │ auto-discovery             and JSON
└─────────────┘
```

1. App searches for `mcp.json` in exe dir, then working dir
2. User message → llama.cpp API (with tool definitions)
3. If model returns `tool_calls` → execute via MCP
4. Tool result sent back to model
5. Model final response displayed to user
6. Stats shown after each response (tokens, time, t/s, context %)

## MCP Configuration

The app reads `mcp.json` from (in order):
1. Same directory as the executable
2. Current working directory

You can also specify a custom path with `--mcp-config`.

### Standard mcp.json Format

```json
{
  "mcpServers": {
    "server-name": {
      "type": "http",
      "url": "https://example.com/mcp",
      "headers": {
        "Authorization": "Bearer token"
      },
      "enabled": true
    }
  }
}
```

| Field | Required | Description |
|---|---|---|
| `type` | Yes | Must be `"http"` |
| `url` | Yes | MCP server endpoint URL |
| `headers` | No | Additional HTTP headers |
| `enabled` | No | Set `false` to skip (default: `true`) |
