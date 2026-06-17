# prime-agent

`prime-agent` 是 [`pi`](https://github.com/earendil-works/pi) 的 **C/C++ 重实现版本**。

目标：产出一个**单一可执行** `pi`，可在 Linux（含 K3 RISC-V）/ macOS / Windows 上运行，对接 Anthropic / OpenAI / Google / Mistral 等 LLM 提供商。

## 当前能力（V1 + V2 已完成）

| Phase | 能力 | 状态 |
|---|---|---|
| **P0** | 基础库（Result、JSON、日志、路径、文件 I/O、ANSI、flock、Unicode 宽度） | ✅ |
| **P1** | pi_ai + Anthropic Messages + OpenAI Chat Completions Providers | ✅ |
| **P2** | pi_agent 循环 + bash / read / write / edit 工具 | ✅ |
| **P3** | TUI（Terminal、Component、Box、Text、Input、Editor、Footer、Theme）+ 交互模式 | ✅ |
| **P4** | Session 持久化（JSONL + flock） + Settings（global + project 合并） + Auth 存储 | ✅ |
| **P5** | RPC 模式（JSONL over stdin/stdout） + Compaction 启发式 | ✅ |
| **P6** | Google Gemini Provider | ✅ |
| **P7** | 打包脚本（install.sh、release.sh） | ✅ |
| **V2.1** | 真正的 Compaction（LLM 生成摘要） | ✅ |
| **V2.2** | grep / find / ls 工具 | ✅ |
| **V2.3** | HTML Export（standalone dark theme + role badges + 折叠工具调用） | ✅ |
| **V2.4** | OAuth 2.0 PKCE primitives（PKCE / CallbackServer / exchange / refresh） | ✅ |
| **V2.5** | Mistral Provider（3 models：Large / Small / Codestral） | ✅ |
| **V2.6** | /compact slash command 真正工作 | ✅ |
| **V2.7** | /login OAuth command 接入 Anthropic framework | ✅ |
| **V2.8** | Multi-line Editor 组件（Ctrl-J 提交） | ✅ |

详见 [`pi-spec/240-Implementation-Plan.md`](pi-spec/240-Implementation-Plan.md)。

## 平台支持

| 平台 | 状态 | 验证 |
|---|---|---|
| Linux riscv64 (K3 / Bianbu 4.0.1) | **P0** | 已在 K3 上构建并运行 |
| Linux x86_64 | **P0** | CI 待加 |
| Linux aarch64 | **P0** | CI 待加 |
| macOS arm64 | **P0** | 待验证 |
| Windows x64 | **P2** | V3 |

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./build/apps/pi/pi --version
./build/apps/pi/pi --help
./build/apps/pi/pi --list-models
```

### 依赖

| 库 | 来源 | 链接 | 备注 |
|---|---|---|---|
| `libstdc++` | 系统 | 动态 | C++20 |
| `libssl` + `libcrypto` | 系统 | 动态 | TLS + SHA256（PKCE） |
| `nlohmann/json` | `third_party/` | header | 已 vendor |

**不依赖** libcurl / libgit2 / libmd4c / libyaml-cpp：HTTP 用 OpenSSL BIO 直写，gitignore 走简单 walk。

## 使用

### Print 模式（单轮）

```bash
export ANTHROPIC_API_KEY=sk-ant-...
pi -p "say exactly: ok"
pi -p "what's 2+2" --model openai/gpt-4o-mini
```

### JSON 事件流

```bash
pi -p "hi" --json
# {"type":"agent_start"}
# {"type":"turn_start"}
# {"type":"message_update","event":"text_delta","delta":"Hello"}
# ...
# {"type":"tool_execution_start","tool":"bash","args":{"command":"echo hi"}}
# {"type":"tool_execution_end","tool":"bash","isError":false,...}
# {"type":"done","stopReason":"stop","usage":{...}}
```

### RPC 模式（程序化）

```bash
echo '{"type":"ping","id":"x1"}' | pi --mode rpc --api-key sk-...
# {"type":"response","id":"x1","command":"ping","success":true,"data":{"pong":true}}
```

### 交互模式（TUI）

```bash
pi
# 进入 alt-screen，底部输入框
# 命令：/exit, /clear, /help, /model, /new, /compact, /tree, /login <provider>
```

### Session 管理

```bash
pi -c                            # 续最近 session
pi -r                            # picker 选择
pi --session 01J... --export out.html  # 导出 HTML
pi --list-sessions               # 列出全部
```

### HTML Export

```bash
pi --session <id> --export session.html
# 产出 standalone dark-theme HTML，浏览器直接打开
```

### 工具

内置 7 个工具：

| 工具 | 用途 |
|---|---|
| `bash` | fork/execve，timeout，abort-safe，输出截断 30KB |
| `read` | 文件读取（文本按行 / 图像返回 base64） |
| `write` | 原子写（tmp + rename） |
| `edit` | oldString → newString（支持 allOccurrences） |
| `grep` | regex + glob include，行号输出 |
| `find` | glob 查找（`**` 展开为 `*`） |
| `ls` | 目录列表（隐藏 / all 标志） |

## 模型（12 个 from 4 providers）

```bash
pi --list-models
```

| Provider | Model | Context |
|---|---|---|
| Anthropic | claude-sonnet-4-5 | 200K |
| Anthropic | claude-opus-4 | 200K |
| Anthropic | claude-haiku-4 | 200K |
| OpenAI | gpt-4o | 128K |
| OpenAI | gpt-4o-mini | 128K |
| OpenAI | o1 | 200K |
| OpenAI | o3-mini | 200K |
| Google | gemini-2.5-pro | 1M |
| Google | gemini-2.5-flash | 1M |
| Mistral | mistral-large-latest | 128K |
| Mistral | mistral-small-latest | 128K |
| Mistral | codestral-latest | 32K |

## 测试

```bash
cd build && ctest --output-on-failure
```

11 个测试套件覆盖：core / http / sse / anthropic_parser / models / tools (8 assertions) / tui (8) / session (5) / compaction (3) / oauth (4) / editor (7)。

## 打包

```bash
sudo ./tools/install.sh                        # /usr/local/bin/pi
./tools/install.sh --prefix ~/.local           # ~/.local/bin/pi
./tools/release.sh                              # dist/pi-0.1.0-linux-riscv64.tar.gz
sudo ./tools/install.sh --uninstall
```

## 目录

```
apps/pi/         可执行入口
libs/core/       pi_core: 基础设施
libs/http/       pi_http: HTTP/TLS 客户端（OpenSSL BIO）
libs/ai/         pi_ai: LLM Providers（4 家）
libs/agent/      pi_agent: Agent 循环
libs/tui/        pi_tui: TUI 组件库（Editor / Input / Footer / Theme）
libs/coding/     pi_coding: CLI 装配（7 工具 + session + settings + auth + rpc + compaction + html_export + oauth）
third_party/     Vendored 单文件依赖
tests/           跨模块单测
tools/           开发 / 安装 / 打包脚本
pi-spec/         设计规范（240 实施计划 + 230 CPP 映射 + 上游对账）
```

## 已知 V2 限制

1. **OAuth /login 是 framework-only**：Crypto + CallbackServer + token exchange 已实现，但 Anthropic Claude.ai 的具体 client_id 没在官方公开——用户需自行替换。
2. **没有真做 LLM-摘要 compaction 的 cache**：每次 /compact 都重新生成摘要。
3. **没有 Markdown 渲染**：chat 输出是纯文本（md4c 集成是 V3）。
4. **没有扩展 API**：V3 加。
5. **没有 multi-modal 输入**（除了 image）：V3 加 audio / pdf。

## V3 路线图

- Windows 支持
- Markdown 渲染 (md4c)
- 扩展 API（TypeScript-like 钩子）
- AWS Bedrock Converse
- Codex WebSocket
- 多 modal：audio / pdf
- 性能 pass（启动 < 200ms）

## 许可证

MIT（与上游 `pi` 一致）。Vendored 依赖保留各自许可证。
