# 160 · 外部服务清单 (External Services)

> 运行时必须与之通信的远程服务。**C/C++ 端口必须自己实现对这些服务的调用**（不能直接用 npm SDK）。

## 1. LLM Provider 服务

| 服务 | baseUrl | 协议 | Auth | 用在 |
| --- | --- | --- | --- | --- |
| **Anthropic Messages** | `https://api.anthropic.com` | HTTPS + SSE | `x-api-key` / OAuth Bearer | `providers/anthropic.ts` |
| **OpenAI Chat Completions** | `https://api.openai.com/v1` | HTTPS + SSE | `Authorization: Bearer` | `providers/openai-completions.ts` |
| **OpenAI Responses** | `https://api.openai.com/v1` | HTTPS + SSE | `Authorization: Bearer` | `providers/openai-responses.ts` |
| **OpenAI Codex (WebSocket)** | `wss://api.openai.com/v1/responses` (后端 ws) | WSS | OAuth Bearer | `providers/openai-codex-responses.ts` |
| **Azure OpenAI Responses** | 用户配置 | HTTPS + SSE | `api-key` header | `providers/azure-openai-responses.ts` |
| **Google Gemini (genai)** | `https://generativelanguage.googleapis.com` | HTTPS + SSE | `x-goog-api-key` / OAuth | `providers/google.ts` |
| **Google Vertex AI** | `https://{region}-aiplatform.googleapis.com` | HTTPS + SSE | OAuth Bearer | `providers/google-vertex.ts` |
| **Mistral Conversations** | `https://api.mistral.ai/v1` | HTTPS + SSE | `Authorization: Bearer` | `providers/mistral.ts` |
| **AWS Bedrock** | `bedrock-runtime.{region}.amazonaws.com` | HTTPS + SSE | **AWS SigV4** | `providers/amazon-bedrock.ts` |
| **Cloudflare Workers AI** | `https://api.cloudflare.com/client/v4/accounts/{id}/ai/v1` | HTTPS + SSE | `Authorization: Bearer` | `providers/cloudflare.ts` |
| **Cloudflare AI Gateway** | 用户配置 | HTTPS + SSE | `Authorization: Bearer` | `providers/cloudflare.ts` |
| **OpenAI-compatible**（40+ 提供方） | 用户配置 | HTTPS + SSE | varies | `providers/openai-completions.ts` (with compat) |
| **Anthropic-compatible** | 用户配置 | HTTPS + SSE | varies | `providers/anthropic.ts` (with compat) |
| **OpenRouter** | `https://openrouter.ai/api/v1` | HTTPS + SSE | `Authorization: Bearer` | 同上 |
| **Vercel AI Gateway** | 用户配置 | HTTPS + SSE | `Authorization: Bearer` | 同上 |
| **GitHub Copilot** | `https://api.github.com/copilot_internal/...` | HTTPS + SSE | OAuth Bearer | `utils/oauth/github-copilot.ts` |
| **xAI (Grok)** | `https://api.x.ai/v1` | HTTPS + SSE | `Authorization: Bearer` | openai-compat |
| **Groq** | `https://api.groq.com/openai/v1` | HTTPS + SSE | `Authorization: Bearer` | openai-compat |
| **Cerebras** | `https://api.cerebras.ai/v1` | HTTPS + SSE | `Authorization: Bearer` | openai-compat |
| **DeepSeek** | `https://api.deepseek.com/v1` | HTTPS + SSE | `Authorization: Bearer` | openai-compat |
| **NVIDIA NIM** | 用户配置 | HTTPS + SSE | `Authorization: Bearer` | openai-compat |
| **Together AI** | `https://api.together.xyz/v1` | HTTPS + SSE | `Authorization: Bearer` | openai-compat |
| **Fireworks AI** | `https://api.fireworks.ai/inference/v1` | HTTPS + SSE | `Authorization: Bearer` | openai-compat |
| **HuggingFace Inference** | `https://router.huggingface.co/v1` | HTTPS + SSE | `Authorization: Bearer` | openai-compat |
| **Moonshot / Kimi** | `https://api.moonshot.cn/v1` | HTTPS + SSE | `Authorization: Bearer` | openai-compat |
| **Z.AI / Z.ai** | `https://api.z.ai/api/paas/v4` | HTTPS + SSE | `Authorization: Bearer` | openai-compat |
| **Xiaomi MiMo** | `https://api.xiaomi.ai/v1` | HTTPS + SSE | `Authorization: Bearer` | openai-compat |

> **SSE 格式**：所有 HTTP 路径都用 Server-Sent Events (`text/event-stream`)，每行 `data: {...}` 一个 JSON 事件，结束为 `data: [DONE]`（OpenAI 风格）或 `event: message_stop`（Anthropic 风格）。

## 2. OAuth Provider 服务

| 服务 | 端点 | 协议 | 备注 |
| --- | --- | --- | --- |
| **Anthropic OAuth** | `https://console.anthropic.com/oauth/authorize`<br>`https://console.anthropic.com/api/oauth/token`<br>`https://api.anthropic.com/api/oauth/claude_cli_client/create_api_key` | OAuth 2.0 + PKCE | client_id 公开，PKCE 流程 |
| **OpenAI Codex** | `https://auth.openai.com/oauth/authorize`<br>`https://auth.openai.com/api/accounts/oauth2/token` | OAuth 2.0 + PKCE | 双模式：browser callback 或 device code |
| **GitHub Copilot** | `https://github.com/login/device/code`<br>`https://github.com/login/oauth/access_token` | OAuth 2.0 Device Code | 设备码 + 轮询 |

> **本地 OAuth callback**：Anthropic / OpenAI Codex 走 `http://localhost:{port}/callback`，需要本地 HTTP server。C/C++ 端口需自实现：bind to `127.0.0.1:0`、等 1 个 GET、返回 200 HTML、关掉。

## 3. Image Generation 服务

| 服务 | 端点 | 协议 |
| --- | --- | --- |
| **OpenRouter Images** | `https://openrouter.ai/api/v1/images` | HTTPS POST + JSON 响应 |

## 4. 资源加载网络（pi package install）

| 用途 | 协议 |
| --- | --- |
| `git clone <url>` | SSH / HTTPS git |
| `npm install <pkg>` | npm registry HTTPS |
| GitHub/GitLab raw file fetch | HTTPS GET |

> C/C++ 端口建议：调用外部命令 `git` / `npm`，不重新实现。

## 5. 其它"软"外部依赖

| 用途 | 来源 | C/C++ 端口 |
| --- | --- | --- |
| 终端能力检测 | `xterm-256color` / `alacritty` / `iterm2` / `kitty` 等 terminfo / DA1 响应 | 不需要外部服务 |
| 系统浏览器（OAuth） | `open` / `xdg-open` / `start` 命令 | 调用系统命令 |
| 系统剪贴板 | macOS Pasteboard / X11 / Win32 Clipboard | native API（已用 .node） |
| 操作系统版本 / 平台 | `process.platform` / `os.release()` | uname / GetVersionEx |
| 系统 shell | `SHELL` env / `getShellConfig()` | 调用 `which` / 读 `/etc/shells` |

## 6. 不在外部服务但需要的能力

- **图像处理**（resize / format convert）：photon-node WASM → C/C++ 用 libvips / stb_image
- **PKCE / SHA-256**：Web Crypto API → C/C++ 用 OpenSSL EVP
- **JSON 解析/序列化**：JSON.parse / JSON.stringify → C/C++ 用 simdjson / nlohmann/json
- **YAML 解析**：yaml 包 → C/C++ 用 yaml-cpp
- **Glob 匹配**：minimatch → C/C++ 用 fnmatch + 自实现 `**`
- **ANSI 解析**：自实现（仓库内 ansi.ts）→ C/C++ 自实现
- **Markdown 解析**：marked → C/C++ 用 cmark / 自实现简化版
- **Syntax highlight**：highlight.js → C/C++ 用 Pygments C port / syntect

## 7. 完整 baseUrl 列表（来自 `models.generated.ts`）

`packages/ai/src/models.generated.ts` 内有 30+ provider × baseUrl 的真实列表；调用 LLM 时按 model 的 `baseUrl` 拼路径。生成器是 `packages/ai/scripts/generate-models.ts`。
