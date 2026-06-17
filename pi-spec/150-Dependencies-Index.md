# 150 · 依赖梳理总索引 (Dependencies Index)

> 目标受众：**用 C/C++ 重新实现整个系统**。本文档梳理系统对外部 npm 包、第三方库、外部服务、协议的所有依赖，并标注每个依赖在 C/C++ 端口中的去向（直接用 / 自实现 / 找替代）。

## 文档清单

| 编号 | 文档 | 范围 |
| --- | --- | --- |
| [160](./160-External-Services.md) | 外部服务清单 | 必须与之通信的远程 API（Anthropic、OpenAI、Google、AWS、Copilot、Codex、Cloudflare…） |
| [170](./170-Dependencies-pi-ai.md) | `pi-ai` 依赖 | 10 个直接 npm 依赖 + Provider SDK + OAuth 协议 |
| [180](./180-Dependencies-pi-agent-core.md) | `pi-agent-core` 依赖 | 3 个直接依赖 + 间接 |
| [190](./190-Dependencies-pi-tui.md) | `pi-tui` 依赖 | 2 个直接依赖 + 2 个 native module（含 .c 源码） |
| [200](./200-Dependencies-pi-coding-agent.md) | `pi-coding-agent` 依赖 | 19 个直接依赖 + native + WASM |
| [210](./210-Network-Protocols.md) | 网络协议 | HTTP/SSE/WebSocket/JSONL/JSON 协议面 |
| [220](./220-Build-Toolchain-Deps.md) | 构建 / 开发依赖 | 14 个 devDependencies + tools |
| [230](./230-CPP-Port-Mapping.md) | C/C++ 端口映射 | 每个依赖的建议 C/C++ 替代或自实现路径 |

## 1. 直接依赖总数

| 子系统 | 直接 deps | native modules | WASM |
| --- | ---: | ---: | ---: |
| `pi-ai` | 10 | 0 | 0 |
| `pi-agent-core` | 3 | 0 | 0 |
| `pi-tui` | 2 | **2** | 0 |
| `pi-coding-agent` | 19 | **1**（@mariozechner/clipboard，可选） | **1**（photon-node） |
| 根 | 9 devDeps | 0 | 0 |
| **合计（直接）** | **43** | **3** | **1** |

> 间接依赖（lockfile 解析后）约 200+ npm 包；详见 [220-Build-Toolchain-Deps.md](./220-Build-Toolchain-Deps.md) 第 6 节。

## 2. 依赖分类（按用途）

### 2.1 LLM Provider SDK（5 个）

| npm 包 | 大小 | 协议 |
| --- | --- | --- |
| `@anthropic-ai/sdk` 0.91.1 | ~5 MB | Anthropic Messages API（HTTP+SSE） |
| `openai` 6.26.0 | ~10 MB | OpenAI / OpenAI-compatible HTTP+SSE/WS |
| `@google/genai` 1.52.0 | ~3 MB | Gemini + Vertex AI |
| `@mistralai/mistralai` 2.2.1 | ~1 MB | Mistral Conversations |
| `@aws-sdk/client-bedrock-runtime` 3.1048.0 | **~50 MB** | AWS Bedrock Converse（SigV4 签名） |

> C/C++ 端口建议：**全部自实现**。这些 SDK 都很薄，调用的是公开 HTTP API，可以直接用 libcurl 替代。

### 2.2 Proxy / HTTP 工具

| npm 包 | 用途 |
| --- | --- |
| `http-proxy-agent` 7.0.2 | HTTP 代理 |
| `https-proxy-agent` 7.0.6 | HTTPS 代理 |
| `undici` 8.3.0 | Node 内置 HTTP 客户端的替代；global dispatcher |
| `@smithy/node-http-handler` 4.7.3 | AWS SDK 用的 HTTP handler |

> C/C++ 端口建议：用 libcurl 或 cpp-httplix 替代，proxy 支持是标准 HTTP 客户端功能。

### 2.3 解析 / 工具

| npm 包 | 用途 |
| --- | --- |
| `typebox` 1.1.38 | JSON Schema + TS 类型推导 |
| `yaml` 2.9.0 | YAML 解析（frontmatter） |
| `ignore` 7.0.5 | `.gitignore` 解析 |
| `partial-json` 0.1.7 | 部分 JSON 解析（流式 tool_call） |
| `minimatch` 10.2.5 | glob 模式匹配 |
| `glob` 13.0.6 | 文件 glob |
| `hosted-git-info` 9.0.3 | git URL 解析 |
| `semver` 7.8.0 | 语义化版本 |
| `proper-lockfile` 4.1.2 | 文件锁 |
| `jiti` 2.7.0 | 运行时 TS loader（扩展） |
| `cross-spawn` 7.0.6 | 跨平台 spawn |
| `diff` 8.0.4 | unified diff 库 |
| `highlight.js` 10.7.3 | 代码高亮 |
| `marked` 15.0.12 | Markdown 解析 |
| `get-east-asian-width` 1.6.0 | CJK/Emoji 宽度 |
| `chalk` 5.6.2 | 终端颜色（开发用） |

### 2.4 Native（3 个 `.node` 模块 + 1 个 WASM）

| 模块 | 用途 | 平台 |
| --- | --- | --- |
| `@mariozechner/clipboard` 0.3.9 | 系统剪贴板 | macOS / Linux / Windows (NAPI-RS) |
| `darwin-modifiers.node` | macOS 修饰键状态 | macOS（自带 .c 源码） |
| `win32-console-mode.node` | Win32 VT 输入启用 | Windows（自带 .c 源码） |
| `@silvia-odwyer/photon-node` 0.3.4 | 图像 resize | 跨平台（WASM） |

> C/C++ 端口建议：所有 native 模块**重写**（用系统 API + 平台特定代码），photon 换成 libvips / stb_image。

### 2.5 间接（运行时真正被加载但不直接列出）

- `mime` (3.x)
- `chownr`, `fs-minipass`, `minipass`, `tar` (打包)
- `eastasianwidth` (旧版 marked)
- `undici-types` (类型)
- 几十个 AWS / Google SDK 间接依赖（用不到）

## 3. 不直接依赖的（运行时由 Node/Bun 提供）

| 能力 | 来源 | C/C++ 替代 |
| --- | --- | --- |
| 文件系统 | `node:fs` | stdio + POSIX |
| 子进程 | `node:child_process` | fork/execve |
| HTTP server (OAuth callback) | `http.createServer` | 自实现或 libmicrohttpd |
| 终端 raw mode | `tty.ReadStream` | termios (POSIX) / SetConsoleMode (Win) |
| ANSI 处理 | 自实现 | 自实现 |
| 终端能力探测 | 自实现 | 自实现 |
| WebSocket | `ws` (间接) | libwebsockets |
| TLS | `tls` | OpenSSL / mbedTLS |
| DNS | `dns` | c-ares / getaddrinfo |

## 4. 不在 `node_modules` 但运行时接触的"协议契约"

| 协议 | 来源 | C/C++ 端口 |
| --- | --- | --- |
| OAuth 2.0 PKCE | RFC 7636 | 自实现（~200 行） |
| OAuth 2.0 Device Code | RFC 8628 | 自实现（~100 行） |
| AWS SigV4 | AWS docs | 自实现（hash + sign） |
| OpenAI Chat Completions | openai.com/docs/api-reference | 自实现 |
| Anthropic Messages | docs.anthropic.com | 自实现 |
| Google Gemini | ai.google.dev | 自实现 |
| SSE (Server-Sent Events) | HTML5 spec | 自实现 |
| JSON-RPC over JSONL | 自定义 | 自实现 |
| OpenAI Codex WebSocket | 自定义 (openai-codex-responses.ts) | 自实现 |
| Kitty 键盘协议 | sw.kovidgoyal.net/kitty | 自实现 |
| iTerm2 inline image | iterm2.com/documentation | 自实现 |
| OSC 9;4 进度 | gnome.org 提案 | 自实现 |
| OSC 11 背景色 | XTerm docs | 自实现 |

## 5. 顺读建议

1. 先看 [160-External-Services.md](./160-External-Services.md) 了解要连哪些服务
2. 再看 [230-CPP-Port-Mapping.md](./230-CPP-Port-Mapping.md) 拿到每个依赖的 C/C++ 方案
3. 然后按包读 [170](./170-Dependencies-pi-ai.md) → [200](./200-Dependencies-pi-coding-agent.md) 拿详细调用点
4. 最后看 [210-Network-Protocols.md](./210-Network-Protocols.md) 和 [220-Build-Toolchain-Deps.md](./220-Build-Toolchain-Deps.md)
