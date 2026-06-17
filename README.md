# prime-agent

`prime-agent` 是 [`pi`](https://github.com/earendil-works/pi) 的 **C/C++ 重实现版本**。

目标：产出一个**单一可执行** `pi`，可在 Linux（含 K3 RISC-V）/ macOS / Windows 上运行，对接 Anthropic / OpenAI / Google 等 LLM 提供商。

## 当前能力（V1 已完成）

| Phase | 能力 | 状态 |
|---|---|---|
| **P0** | 基础库（Result、JSON、日志、路径、文件 I/O、ANSI、flock、Unicode 宽度） | ✅ |
| **P1** | pi_ai + Anthropic Messages + OpenAI Chat Completions Providers | ✅ |
| **P2** | pi_agent 循环 + bash / read / write / edit 工具 | ✅ |
| **P3** | TUI（Terminal、Component、Box、Text、Input、Footer、Theme）+ 交互模式 | ✅ |
| **P4** | Session 持久化（JSONL + flock） + Settings（global + project 合并） + Auth 存储 | ✅ |
| **P5** | RPC 模式（JSONL over stdin/stdout） + Compaction 启发式 | ✅ |
| **P6** | Google Gemini Provider | ✅ |
| **P7** | 打包脚本（install.sh、release.sh） | ✅ |

详见 [`pi-spec/240-Implementation-Plan.md`](pi-spec/240-Implementation-Plan.md)。

## 平台支持

| 平台 | 状态 | 验证 |
|---|---|---|
| Linux riscv64 (K3 / Bianbu 4.0.1) | **P0** | 已在 K3 上构建并运行 |
| Linux x86_64 | **P0** | CI 待加 |
| Linux aarch64 | **P0** | CI 待加 |
| macOS arm64 | **P0** | 待验证 |
| Windows x64 | **P2** | V2 |

## 构建

```bash
# 配置
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build -j$(nproc)

# 产物
./build/apps/pi/pi --version
./build/apps/pi/pi --help
./build/apps/pi/pi --list-models
```

### 依赖

| 库 | 来源 | 链接 | 备注 |
|---|---|---|---|
| `libstdc++` | 系统 | 动态 | C++20 |
| `libssl` + `libcrypto` | 系统 | 动态 | TLS + HMAC |
| `nlohmann/json` | `third_party/` | header | 已 vendor |
| `libcurl` | 可选 | 动态 | V2 替换 OpenSSL BIO |
| `libmd4c` | 可选 | 动态 | Markdown 渲染（V2） |
| `libyaml-cpp` | 可选 | 动态 | Settings YAML（V2） |
| `libgit2` | 可选 | 动态 | gitignore（V2） |

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

echo '{"type":"prompt","id":"x2","text":"say ok"}' \
    | pi --mode rpc --api-key sk-ant-...
# agent events...
# {"type":"response","id":"x2","command":"prompt","success":true}
```

### 交互模式（TUI）

```bash
pi
# 进入 alt-screen，底部输入框，键入消息
# 命令：/exit, /clear, /help, /model, /new
```

### Session 管理

```bash
pi -c                # 续最近 session
pi -r                # picker 选择
pi --session 01J...  # 8+ 字符前缀匹配
pi --list-sessions   # 列出全部
```

### 工具

内置 4 个工具，全部开箱即用：

| 工具 | 用途 |
|---|---|
| `bash` | fork/execve，timeout，abort-safe，输出截断 30KB |
| `read` | 文件读取（文本按行 / 图像返回 base64） |
| `write` | 原子写（tmp + rename） |
| `edit` | oldString → newString（支持 allOccurrences） |

## 模型

```bash
pi --list-models
```

内置 9 个模型：

| Provider | Model | Context | Reasoning |
|---|---|---|---|
| Anthropic | claude-sonnet-4-5 | 200K | ✅ |
| Anthropic | claude-opus-4 | 200K | ✅ |
| Anthropic | claude-haiku-4 | 200K | ✅ |
| OpenAI | gpt-4o | 128K | |
| OpenAI | gpt-4o-mini | 128K | |
| OpenAI | o1 | 200K | ✅ |
| OpenAI | o3-mini | 200K | ✅ |
| Google | gemini-2.5-pro | 1M | ✅ |
| Google | gemini-2.5-flash | 1M | ✅ |

## 测试

```bash
cd build && ctest --output-on-failure
```

9 个测试覆盖：core, http, sse, anthropic_parser, models, tools, tui, session, compaction。

## 打包

```bash
# 安装到 /usr/local/bin
sudo ./tools/install.sh

# 安装到 ~/.local
./tools/install.sh --prefix ~/.local

# 打 tarball
./tools/release.sh
# -> dist/pi-0.1.0-linux-riscv64.tar.gz

# 卸载
sudo ./tools/install.sh --uninstall
```

## 目录

```
apps/pi/         可执行入口
libs/core/       pi_core: 基础设施
libs/http/       pi_http: HTTP/TLS 客户端（OpenSSL BIO）
libs/ai/         pi_ai: LLM Providers
libs/agent/      pi_agent: Agent 循环
libs/tui/        pi_tui: TUI 组件库
libs/coding/     pi_coding: CLI 装配
third_party/     Vendored 单文件依赖
tests/           跨模块单测
tools/           开发 / 安装 / 打包脚本
pi-spec/         设计规范（240 实施计划 + 230 CPP 映射 + 上游对账）
```

## 已知 V1 限制

1. 不支持 Windows（V2 补）。
2. 不支持 Bedrock Converse / Codex WebSocket / Mistral（V2 补）。
3. Compaction 只估算了 token 数，**没有真做 LLM 摘要**（V2 接上 LLM）。
4. 没有 HTML 导出（V2）。
5. OAuth 没做（V2 加 PKCE + Device Code）。
6. 扩展 API 没做（V2）。
7. TUI 多行 Editor 没做（V1 只用单行 Input）。

## 许可证

MIT（与上游 `pi` 一致）。Vendored 依赖保留各自许可证。
