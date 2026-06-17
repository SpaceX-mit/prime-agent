# 230 · C/C++ 端口映射 (C/C++ Port Mapping)

> 每个 npm 依赖 → 推荐的 C/C++ 替代或"自实现"路径。

## 1. 映射表

| npm 包 | 版本 | 类别 | C/C++ 端口方案 | 行数估算 |
| --- | --- | ---: | --- | ---: |
| `@anthropic-ai/sdk` | 0.91.1 | LLM SDK | **自实现**：HTTP + SSE + 自解析 | ~600 |
| `openai` | 6.26.0 | LLM SDK | **自实现**：HTTP + SSE + WSS（Codex） | ~1800 |
| `@google/genai` | 1.52.0 | LLM SDK | **自实现**：HTTP + SSE | ~500 |
| `@mistralai/mistralai` | 2.2.1 | LLM SDK | **复用 OpenAI 实现**（协议同） | ~0 |
| `@aws-sdk/client-bedrock-runtime` | 3.1048.0 | LLM SDK | **自实现 SigV4** + 走 Bedrock 上的 Claude | ~800 |
| `@smithy/node-http-handler` | 4.7.3 | AWS util | **不需要** | — |
| `http-proxy-agent` | 7.0.2 | proxy | **libcurl** `CURLOPT_PROXY` | 0 |
| `https-proxy-agent` | 7.0.6 | proxy | **libcurl** `CURLOPT_PROXY` | 0 |
| `undici` | 8.3.0 | HTTP client | **libcurl** | 0 |
| `typebox` | 1.1.38 | JSON Schema | **自实现简化版**（仅工具参数） | ~200 |
| `partial-json` | 0.1.7 | 流式 JSON | **自实现** | ~80 |
| `yaml` | 2.9.0 | YAML 解析 | **yaml-cpp** | 0 |
| `ignore` | 7.0.5 | gitignore | **libgit2** `git_ignore_*` 或自实现 | ~300 |
| `chalk` | 5.6.2 | 终端颜色 | **自实现 ANSI 转义** | ~100 |
| `cross-spawn` | 7.0.6 | 跨平台 spawn | **POSIX fork/execve + Win32 CreateProcess** | ~300 |
| `diff` | 8.0.4 | unified diff | **自实现 Myers diff** | ~150 |
| `glob` | 13.0.6 | 文件 glob | **POSIX fnmatch + walk** | ~200 |
| `highlight.js` | 10.7.3 | 代码高亮 | **pygments-c** 或自实现简化版 | ~1500 |
| `hosted-git-info` | 9.0.3 | git URL 解析 | **自实现** | ~80 |
| `minimatch` | 10.2.5 | glob 匹配 | **自实现**（含 `**`） | ~200 |
| `proper-lockfile` | 4.1.2 | 文件锁 | **POSIX flock + lockfile 名包含 UUID** | ~80 |
| `semver` | 7.8.0 | 语义化版本 | **自实现** | ~150 |
| `jiti` | 2.7.0 | TS loader | **跳过**（C/C++ 不做扩展） | — |
| `marked` | 15.0.12 | Markdown | **md4c**（纯 C） | 0 |
| `get-east-asian-width` | 1.6.0 | CJK 宽度 | **自实现**（依 Unicode East Asian Width 表） | ~60 |
| `@mariozechner/clipboard` | 0.3.9 | 系统剪贴板 | **自实现**（macOS NSPasteboard / Win32 / Linux xclip-wl-paste） | ~400 |
| `@silvia-odwyer/photon-node` | 0.3.4 | 图像处理 | **stb_image + stb_image_resize** | 0 |
| `mime` | 4.x | MIME 探测 | **自实现**（按扩展名） | ~50 |
| `tar` | 7.x | tar 打包 | **libarchive** | 0 |
| `minipass` | 4.x | 流处理 | **std::istream** | 0 |
| `proper-lockfile` 依赖的 lib | — | — | — | — |
| 各种 `@types/*` | — | TS 类型 | 不需要 | — |
| 各种 `@aws-sdk/*` 间接 | — | AWS | 全部跳过 | — |
| 各种 `@smithy/*` 间接 | — | AWS | 全部跳过 | — |

## 2. 推荐 C/C++ 库清单

### 2.1 系统已有

| 库 | 用途 | 备注 |
| --- | --- | --- |
| `libcurl` | HTTP 客户端 | Linux/macOS/Windows 都有 |
| `OpenSSL` | TLS + HMAC + SHA256 | libcurl 依赖 |
| `POSIX threads` | 并发 | Linux/macOS |
| `termios.h` | 终端 raw mode | Linux/macOS |
| `<windows.h>` | 终端模式 + 剪贴板 | Windows |

### 2.2 推荐引入

| 库 | 用途 | 许可证 | 大小 | 来源 |
| --- | --- | --- | ---: | --- |
| `nlohmann/json` | JSON | MIT | header-only | <https://github.com/nlohmann/json> |
| `md4c` | Markdown | MIT/Expat | ~150 KB | <https://github.com/mity/md4c> |
| `yaml-cpp` | YAML | MIT | ~500 KB | <https://github.com/jbeder/yaml-cpp> |
| `libgit2` | git + gitignore | GPLv2+ | ~5 MB | <https://libgit2.org> |
| `stb_image` + `stb_image_resize` | 图像 | MIT/Public Domain | header-only | <https://github.com/nothings/stb> |
| `libwebsockets` | WebSocket（Codex） | MIT | ~500 KB | <https://libwebsockets.org> |
| `pygments-c` | 代码高亮 | BSD | ~1 MB | <https://github.com/risusdev/pygments-c>（或自实现） |
| `c-ares` | DNS | MIT | ~200 KB | <https://c-ares.org> |

### 2.3 不推荐

| 库 | 不推荐原因 |
| --- | --- |
| AWS SDK C++ | 50+ MB；只调一个 API；自实现 SigV4 更轻 |
| Boost | 太大；C++17 标准库够用 |
| Qt / GTK | TUI 不需要 GUI |
| libmicrohttpd | OAuth callback 太轻，~100 行可写 |
| libfmt | 标准库 `std::format` 够（C++20） |

## 3. 关键自实现清单

### 3.1 LLM 协议解析（~3000 行）

```cpp
// 伪码
class StreamParser {
    enum class Provider { OpenAI, OpenAIResponses, OpenAICodexWS, Anthropic, Gemini, Mistral };
    
    std::optional<AssistantMessageEvent> feed(std::string_view chunk);
    AssistantMessage finish();   // 拿 stopReason / usage
};
```

每个 provider 一个类，共用 SSE 解析器：
- `SseParser` 通用：~150 行
- `OpenAIChatParser`：~500 行
- `OpenAIResponsesParser`：~400 行
- `OpenAICodexWsParser`：~1500 行（WebSocket + 私有 header）
- `AnthropicMessagesParser`：~500 行
- `GeminiParser`：~400 行（JSON array 格式）
- `MistralParser`：~50 行（复用 OpenAI）

### 3.2 OAuth 流程（~600 行）

```cpp
class OAuthFlow {
    enum class Type { PKCE, DeviceCode };
    
    // PKCE
    std::pair<std::string, std::string> generatePKCE();  // verifier, challenge
    std::string authorizationUrl();
    std::string startCallbackServer(uint16_t port);  // 返 URL
    
    // Device Code
    DeviceCodeInfo requestDeviceCode();
    Credentials pollForToken(DeviceCodeInfo);
    
    // Common
    Credentials exchangeCode(std::string code);
    Credentials refresh(Credentials c);
};
```

### 3.3 TUI 差分渲染（~800 行）

```cpp
class TUI {
    std::vector<Component*> root;
    std::string prevFrame;
    
    void add(Component* c);
    void render(int width);
    void writeDiff();  // 行级 diff，写到 stdout
};

class Component {
    virtual std::vector<std::string> render(int width) = 0;
    virtual void invalidate() {}
};
```

### 3.4 工具实现（~2000 行）

| 工具 | 复杂度 | 备注 |
| --- | --- | --- |
| `bash` | ~400 行 | fork/execve + stream pipe + kill |
| `read` | ~150 行 | `fopen` + 行范围 + 图像 MIME 探测 |
| `write` | ~50 行 | `fopen + fputs` |
| `edit` | ~250 行 | oldString 匹配 + multi-edit + atomic write |
| `grep` | ~300 行 | 自实现简单 ripgrep（足够日常） |
| `find` | ~150 行 | glob + walk + ignore |
| `ls` | ~100 行 | `opendir/readdir` + ignore |

### 3.5 Agent 循环（~500 行）

```cpp
class AgentLoop {
    AgentContext ctx;
    AgentLoopConfig config;
    StreamFn streamFn;
    AbortSignal* signal;
    
    EventStream<AgentEvent, AgentMessage[]> run(std::vector<AgentMessage> prompts);
};
```

`convertToLlm` 钩子、8 个 hooks、并行/顺序模式、事件发射等。

### 3.6 Compaction（~300 行）

`findCutPoint` / `shouldCompact` / `generateSummary` 等纯函数。

## 4. 头文件 / 包组织

```cpp
// 主入口
#include <pi_ai/provider.hpp>
#include <pi_ai/stream.hpp>
#include <pi_agent/agent.hpp>
#include <pi_agent/compaction.hpp>
#include <pi_tui/tui.hpp>
#include <pi_coding/agent_session.hpp>
#include <pi_coding/tools.hpp>
#include <pi_coding/extensions.hpp>
```

## 5. 编译产物

| 目标 | 行数 | 二进制大小 | 依赖 |
| --- | ---: | ---: | --- |
| `pi` CLI | ~50,000 | ~5 MB 静态 | libcurl, OpenSSL, libgit2, yaml-cpp, md4c |
| `pi-tui` lib | ~5,000 | (静态链入) | 无 |
| `pi-ai` lib | ~8,000 | (静态链入) | libcurl, OpenSSL |
| `pi-agent` lib | ~3,000 | (静态链入) | 无 |
| `pi-coding` lib | ~30,000 | (静态链入) | stb_image, yaml-cpp |

## 6. 平台矩阵

| 平台 | 优先级 | 备注 |
| --- | --- | --- |
| Linux x86_64 | P0 | 主目标 |
| Linux aarch64 | P0 | 通用 ARM |
| macOS arm64 | P0 | 开发者主用 |
| macOS x86_64 | P1 | 旧 Mac |
| Windows x64 | P1 | 用户群广 |
| Windows arm64 | P2 | 新硬件 |
| **Linux riscv64** | **P0** | K3 项目核心目标 |

**RISC-V 注意点**：
- 不依赖 go（避免 `tsgo`）
- 不依赖 bun
- 编译用 `riscv64-linux-gnu-g++` 14+
- 动态链 `libcurl4` / `libssl3` / `libgit2-1.7` / `libyaml-cpp`（Debian K3 仓库都有）

## 7. 风险与跳过

| 风险 | 缓解 |
| --- | --- |
| AWS Bedrock 协议复杂（二进制 event stream） | 默认走 Bedrock 上的 Claude，**用 Anthropic provider 路径**，不实现 event stream |
| OpenAI Codex WSS 私有协议 | 第一版**跳过 Codex**；先支持 API key 模式 |
| highlight.js 10.7.3 是最后一版 ES5，新版 ESM | 用 pygments-c 或**只支持 10-15 种主流语言** |
| marked 15.x ESM-only | 用 md4c |
| 终端协议碎片化 | 至少支持 xterm / alacritty / iTerm2 / kitty；其它 fallback |
| Pygments C port 维护弱 | 考虑简单实现（关键字 / 字符串 / 注释高亮） |

## 8. 实施顺序建议

```
1. pi-ai 基础:    LLM 协议 (Anthropic + OpenAI) + 工具调用 + 简单 OAuth
2. pi-agent 基础: Agent 循环 + 钩子
3. 工具:          read, write, edit, bash
4. TUI 基础:      TUI + Box + Text + Editor
5. pi-coding:     AgentSession + Settings + ResourceLoader
6. 模式:          Print + Interactive
7. Compaction:    auto + manual
8. 扩展:          v1 钩子（不暴露 jiti）
9. 其余 provider: Google, Mistral, Azure
10. 其余协议:     WebSocket (Codex), AWS SigV4
11. HTML 导出
12. RPC 模式
```

每步都要：
- 写单测（doctest）
- 跑 `pi -p "say ok"` 烟测
- 在 K3 上验证（如可用）
