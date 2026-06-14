# llm-chat — MCP Destekli Terminal Sohbeti

llama.cpp ve MCP sunucusuna bağlanarak terminal üzerinden sohbet etmenizi sağlayan C++ uygulaması.

## Gereksinimler

- C++17 derleyici (g++ ≥ 9, clang ≥ 10)
- CMake ≥ 3.16
- libcurl (geliştirme kütüphanesi)
- GNU Readline (geliştirme kütüphanesi)
- nlohmann/json (geliştirme kütüphanesi)

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

## Derleme

```bash
# Proje kök dizininde
cmake -B build
cmake --build build
```

Derlenen binary: `build/llm-chat`

## Kullanım

### 1. llama.cpp sunucusunu başlat

```bash
# llama.cpp server (OpenAI-compatible API)
llama-server -m <model.gguf> --port 8080
```

### 2. MCP sunucusunu başlat

```bash
# Mevcut MCP sunucusu (server.py)
uv run server.py
```

### 3. llm-chat'i çalıştır

```bash
./build/llm-chat
```

## Ortam Değişkenleri

| Değişken | Varsayılan | Açıklama |
|---|---|---|
| `LLM_CHAT_LLM_URL` | `http://localhost:8080` | llama.cpp sunucu adresi |
| `LLM_CHAT_MCP_URL` | `http://localhost:8000/mcp` | MCP sunucu adresi |
| `LLM_CHAT_MAX_CONTEXT` | `8192` | Model max context token sayısı (yüzdelik hesaplama için) |

```bash
export LLM_CHAT_LLM_URL="http://10.0.0.1:8080"
export LLM_CHAT_MCP_URL="http://10.0.0.1:8000/mcp"
export LLM_CHAT_MAX_CONTEXT=4096
./build/llm-chat
```

## Sohbet Komutları

| Komut | Açıklama |
|---|---|
| `/quit` veya `/exit` | Çıkış |
| `/help` | Komut listesini göster |
| `/clear` | Sohbet geçmişini temizle |
| `/tools` | MCP araçlarını listele |

## Mimarisi

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

1. Kullanıcı mesajı → llama.cpp API (tool tanımlarıyla)
2. Model `tool_calls` dönerse → MCP üzerinden araç çalıştırılır
3. Araç sonucu modele geri gönderilir
4. Model nihai yanıtı döndüğünde kullanıcıya gösterilir
5. Her yanıt sonunda token sayısı, süre ve context yüzdesi gösterilir
