# 240 · 实现计划 (Implementation Plan)

> 把 [230-CPP-Port-Mapping](./230-CPP-Port-Mapping.md) §8 的 12 步落地为**可执行的阶段**，每阶段都有可验证的产物。

## 0. 战略与边界

### 0.1 目标

| 维度 | 目标 |
|---|---|
| **上游** | `https://github.com/earendil-works/pi` (v0.79.4) |
| **产物** | 单文件可执行 `pi`（Linux/macOS/Windows 各一份），体积 5–10 MB 静态 |
| **运行时依赖** | libssl + libcurl（+ libgit2/libyaml-cpp/libmd4c 可选） |
| **平台 P0** | Linux x86_64、Linux aarch64、Linux **riscv64 (K3)**、macOS arm64 |
| **语言** | C++20（C 17 写在不可避免处） |
| **构建** | CMake ≥ 3.20 |
| **测试** | doctest（header-only） |
| **包管理** | 仅 vendoring + 系统 pkg；不引入 conan/vcpkg（K3 没有） |
| **第一版能跑** | `pi -p "say ok"` 在 K3 上通，对接 Anthropic API key |

### 0.2 非目标（V1）

- ❌ Windows（V1 仅 Linux/macOS，Windows 由 V2 补）
- ❌ AWS Bedrock / Google Vertex（V1 直连 Anthropic、OpenAI 即可）
- ❌ Codex WebSocket（协议私有，V2 再说）
- ❌ 扩展系统（V1 只支持核心工具集）
- ❌ 浏览器 OAuth callback（V1 只支持 `--api-key` + 环境变量 + `/login` device code）
- ❌ HTML 导出（V2）
- ❌ MCP 协议（V2，依赖 JSON-RPC）

### 0.3 设计原则

1. **接口先行**：每个 Phase 都先把 `*.hpp` 公开 API 定下来（参考 [90-Interfaces](./90-Interfaces.md)），再写 `.cpp`。
2. **测试驱动**：doctest 单测 + 一条端到端烟测（`pi -p`）当作可执行规约。
3. **head-of-line 阻塞流**：LLM 流必须边到边推（不能"攒满一帧再吐"），用 callback + ring buffer。
4. **错误不抛**：参考 60 §1，`streamSimple` 永不抛；错误落 `stopReason: "error"`。
5. **跨平台条件编译**：`<unistd.h>` vs `<windows.h>`、`<termios.h>` vs `<consoleapi.h>`、POSIX `fork` vs `CreateProcess`。

---

## 1. 项目骨架（V1 最终布局）

```
prime-agent/                           # 本仓库根
├── CMakeLists.txt                     # 顶层构建
├── README.md                          # 构建 + 运行说明
├── .gitignore
├── cmake/                             # 自定义 CMake 工具脚本
│   ├── FindMbedTLS.cmake
│   └── PiPlatform.cmake
├── third_party/                       # vendored 单文件库
│   ├── nlohmann/json.hpp
│   ├── doctest.h
│   ├── stb_image.h
│   └── md4c.h                         # 占位；优先用系统包
├── libs/                              # 我们的内部库
│   ├── core/                          # pi_core: 基础设施（无外部 dep）
│   │   ├── include/pi_core/
│   │   │   ├── result.hpp             # Result<T,E> 类型
│   │   │   ├── error.hpp              # ErrorKind + Error
│   │   │   ├── log.hpp                # 异步日志
│   │   │   ├── json.hpp               # nlohmann 包装
│   │   │   ├── env.hpp                # 环境变量
│   │   │   ├── path.hpp               # 路径拼接、规范化、~ 展开
│   │   │   ├── strutil.hpp            # 字符串工具（trim/split/lower/...）
│   │   │   ├── ansi.hpp               # ANSI 转义
│   │   │   ├── file_io.hpp            # read_file/write_file/exists
│   │   │   ├── lockfile.hpp           # flock 包装
│   │   │   └── unicode_width.hpp      # East Asian Width
│   │   └── src/...
│   ├── http/                          # pi_http: HTTP/TLS 客户端（基于 OpenSSL）
│   │   ├── include/pi_http/
│   │   │   ├── http_client.hpp
│   │   │   ├── http_request.hpp
│   │   │   ├── http_response.hpp
│   │   │   └── sse_parser.hpp
│   │   └── src/...
│   ├── ai/                            # pi_ai: LLM providers
│   │   ├── include/pi_ai/
│   │   │   ├── types.hpp              # Message, AssistantMessage, Model, ...
│   │   │   ├── event_stream.hpp       # AsyncIterable 替代
│   │   │   ├── provider.hpp           # Provider 基类
│   │   │   ├── registry.hpp           # registerApiProvider/getApiProvider
│   │   │   ├── models.hpp             # 内置 MODELS 表
│   │   │   ├── stream_simple.hpp
│   │   │   └── providers/
│   │   │       ├── anthropic.hpp
│   │   │       ├── openai_chat.hpp
│   │   │       ├── openai_responses.hpp
│   │   │       ├── google_gemini.hpp
│   │   │       └── mistral.hpp
│   │   └── src/...
│   ├── agent/                         # pi_agent: Agent 循环
│   │   ├── include/pi_agent/
│   │   │   ├── agent_loop.hpp
│   │   │   ├── agent_context.hpp
│   │   │   ├── agent_message.hpp
│   │   │   ├── tool.hpp
│   │   │   └── hooks.hpp
│   │   └── src/...
│   ├── tui/                           # pi_tui: TUI 组件库
│   │   ├── include/pi_tui/
│   │   │   ├── terminal.hpp
│   │   │   ├── tui.hpp
│   │   │   ├── component.hpp
│   │   │   ├── text.hpp
│   │   │   ├── box.hpp
│   │   │   ├── editor.hpp
│   │   │   ├── input.hpp
│   │   │   ├── select_list.hpp
│   │   │   ├── markdown.hpp
│   │   │   ├── theme.hpp
│   │   │   ├── footer.hpp
│   │   │   └── keybindings.hpp
│   │   └── src/...
│   └── coding/                        # pi_coding: CLI 装配
│       ├── include/pi_coding/
│       │   ├── agent_session.hpp
│       │   ├── args.hpp
│       │   ├── session_manager.hpp
│       │   ├── settings_manager.hpp
│       │   ├── auth_storage.hpp
│       │   ├── model_registry.hpp
│       │   ├── resource_loader.hpp
│       │   ├── system_prompt.hpp
│       │   ├── bash_executor.hpp
│       │   ├── tools/
│       │   │   ├── read_tool.hpp
│       │   │   ├── write_tool.hpp
│       │   │   ├── edit_tool.hpp
│       │   │   └── bash_tool.hpp
│       │   └── modes/
│       │       ├── interactive.hpp
│       │       ├── print.hpp
│       │       └── rpc.hpp
│       └── src/...
├── apps/pi/                           # 可执行入口
│   ├── main.cpp
│   └── pi.service                     # systemd unit（可选）
├── tests/                             # 跨模块单测
│   ├── test_core.cpp
│   ├── test_http.cpp
│   ├── test_sse.cpp
│   ├── test_anthropic_parser.cpp
│   ├── test_agent_loop.cpp
│   └── test_session.cpp
└── tools/                             # 开发辅助脚本
    ├── smoke_pi.sh                    # ./tools/smoke_pi.sh
    └── release.sh                     # 打包 deb + tar.gz
```

## 2. 依赖矩阵（最终态）

| 库 | 来源 | 链接 | 必须阶段 | K3 上可用 |
|---|---|---|---|---|
| `libstdc++` | 系统 | 动态 | P0 | ✅ |
| `libssl` / `libcrypto` | 系统 | 动态 | P0 | ✅ 已装 |
| `nlohmann/json` | vendor (`third_party/`) | header | P0 | ✅ |
| `doctest` | vendor | header | P0 | ✅ |
| `stb_image` | vendor | header | P2+（图像） | ✅ |
| `libcurl`（后续替换 OpenSSL BIO） | 系统 | 动态 | P1（可选）/P3（推荐） | ⚠️ 需装 `libcurl4-openssl-dev` |
| `libmd4c` | 系统 | 动态 | P4（Markdown） | ⚠️ 需装 `libmd4c-dev` |
| `libyaml-cpp` | 系统 | 动态 | P5（settings YAML） | ⚠️ 需装 `libyaml-cpp-dev` |
| `libgit2` | 系统 | 动态 | P5（gitignore） | ⚠️ 需装 `libgit2-dev` |

**V1 策略**：P0-P1 完全用 OpenSSL BIO 直写 HTTP（无需 libcurl headers）。P3+ 切换到 libcurl（如已装），未装则保留 BIO。

## 3. 阶段划分

> 每个阶段结尾都有"**阶段验收**"——可以独立 demo 或通过单测。

### Phase 0 — 基础与骨架（**1-2 天**）

**目标**：仓库能编译，可执行 `pi --version` 跑出 `0.1.0`。

**交付**：
- [x] 仓库骨架（上面的目录结构）
- [x] `CMakeLists.txt` + 顶层构建脚本
- [x] `pi_core` 库的以下模块：
  - `Result<T,E>` + `Error` 类型
  - 日志（同步 + 文件输出）
  - JSON 包装（基于 nlohmann）
  - 路径工具（`~` 展开、`PathBuf`）
  - 字符串工具
  - ANSI 转义
  - 文件 I/O
  - flock 包装
  - Unicode 宽度（简易 East Asian Width 表）
- [x] `apps/pi/main.cpp` 解析 `--help`、`--version`，其它参数走 stub
- [x] 单测：test_core.cpp，ctest 通过

**验收**：
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/apps/pi/pi --version      # 输出 pi 0.1.0
./build/apps/pi/pi --help         # 输出 Usage
ctest --test-dir build --output-on-failure
```

---

### Phase 1 — pi_ai 协议与 Anthropic Provider（**3-5 天**）

**目标**：`streamSimple` 能调 Anthropic Messages API 流式输出。

**交付**：
- [ ] `pi_http` 库：
  - `HttpClient`（连接池、proxy、timeout、TLS via OpenSSL BIO）
  - `HttpRequest` / `HttpResponse`
  - SSE 解析器（`SseParser`）
- [ ] `pi_ai` 库：
  - `EventStream<T,R>`（模板，类 `AsyncIterable`）
  - `Message` / `AssistantMessage` / `ToolCall` 类型
  - `Model` / `Provider` / `StreamOptions` 接口
  - `AnthropicProvider`（Messages API + SSE 流）
  - `OpenAIChatProvider`（Chat Completions + SSE 流）
  - `OpenAIResponsesProvider`（如果时间允许）
  - 内置 `MODELS` 表（5-10 个核心模型：claude-sonnet-4.5, claude-opus-4, gpt-4o, gpt-4.1, ...）
- [ ] `apps/pi/main.cpp` 接入 `streamSimple`，`pi -p "say ok"` 直接流到 stdout

**验收**：
```bash
export ANTHROPIC_API_KEY=sk-ant-...
./build/apps/pi/pi -p "say exactly: ok"
# 期望：stdout 输出 "ok"（可能含前后空白）
./build/apps/pi/pi --list-models
# 期望：列出所有内置模型
```

---

### Phase 2 — pi_agent 循环与工具基础（**3-4 天**）

**目标**：能在程序内跑一个 Agent 循环 + 调工具。

**交付**：
- [ ] `pi_agent` 库：
  - `AgentLoop` 类
  - `AgentContext` / `AgentMessage` / `AgentEvent`
  - 钩子：`beforeToolCall` / `afterToolCall`
  - `convertToLlm` 默认实现
  - `EventStream<AgentEvent>` 集成
- [ ] `pi_coding` 库：
  - `BashExecutor`（fork/execve、pipe、signal、timeout、zombie 清理）
  - `read` 工具（含图像 MIME 探测）
  - `write` 工具
  - `edit` 工具（含 multi-edit、原子写）
  - `bash` 工具
- [ ] 测试：test_agent_loop.cpp 跑出"agent 调用 bash 工具 → 拿到结果 → 续答"

**验收**：
```bash
./build/apps/pi/pi -p "create /tmp/pi-test.txt with content 'hello' and verify it"
# 期望：agent 自主调用 write → read → 返回成功
```

---

### Phase 3 — TUI 基础（**5-7 天**）

**目标**：交互模式能跑（裸输入框 + 输出区 + 底部状态栏）。

**交付**：
- [ ] `pi_tui` 库：
  - `Terminal`（termios raw mode、恢复、信号处理、resize 监听）
  - `TUI` 主类（Component 树 + 差分渲染）
  - `Component` 基类
  - `Box` / `Text` / `Spacer`
  - `Input`（单行）
  - `Editor`（多行，行编辑、kill ring、history）
  - `SelectList`（带 fuzzy）
  - `Footer`（模式、模型、token）
  - `Keybindings`（键映射 + Kitty 协议协商）
  - `Theme`（ANSI 颜色定义）
- [ ] `InteractiveMode` 骨架（输入 → agent → 输出渲染）

**验收**：
```bash
./build/apps/pi/pi
# 出现 TUI：底部输入框、上面输出区、键入 "hi" + Enter 触发 agent
# /exit 退出，终端恢复干净
```

---

### Phase 4 — pi_coding 装配（**4-5 天**）

**目标**：`AgentSession` 装好，session 持久化、settings、auth 跑通。

**交付**：
- [ ] `AgentSession` 类
- [ ] `ArgsParser`（支持 `-p`、`--mode`、`--provider`、`--model`、`--thinking`、`@file`、`-c`、`-r`、`--export`、`--list-models`、`--api-key`、`--no-skills`、`--no-extensions`）
- [ ] `SessionManager`（JSONL 写、proper-lockfile 包装的 flock）
- [ ] `SettingsManager`（`~/.pi/agent/settings.json` + `.pi/settings.json` 合并）
- [ ] `AuthStorage`（`~/.pi/agent/auth.json`、flock、refresh）
- [ ] `ModelRegistry`（内置 + 用户）
- [ ] `ResourceLoader`（skills / themes / agents.md）
- [ ] `SystemPrompt` 构造
- [ ] `list-models` / `version` / `help` / `--export` 完整工作

**验收**：
```bash
./build/apps/pi/pi --version
./build/apps/pi/pi --list-models | head -20
./build/apps/pi/pi -p "say ok"   # 单轮通过
./build/apps/pi/pi -c            # 续最近 session
./build/apps/pi/pi -p "echo $HOME"  # bash 工具能跑
ls ~/.pi/agent/sessions/         # 有新 jsonl 文件
```

---

### Phase 5 — 模式与扩展（**5-7 天**）

**目标**：Print / Interactive / RPC 三模式都通，OAuth 登录通，Compaction 通。

**交付**：
- [ ] `PrintMode`（`-p` / `--mode json`）
- [ ] `InteractiveMode`（slash 命令：`/login`、`/model`、`/compact`、`/session`、`/tree`、`/reload`、`/clear`、`/exit`）
- [ ] `RpcMode`（`--mode rpc`，JSONL over stdin/stdout）
- [ ] `OAuthFlow`：
  - PKCE（Claude.ai）
  - Device code（备用）
- [ ] `Compactor`（自动 + 手动 `/compact`）
- [ ] 简易 extensions 加载（`ctx.registerTool`、`ctx.sendMessage`）

**验收**：
```bash
# Print
./build/apps/pi/pi -p "say ok"
./build/apps/pi/pi --mode json -p "say ok" | jq -c '.type'

# Interactive
./build/apps/pi/pi
# /login claude → 浏览器 → 完成
# /compact → session 出现 compaction entry

# RPC
echo '{"type":"prompt","text":"say ok"}' | ./build/apps/pi/pi --mode rpc
```

---

### Phase 6 — 其余 Provider 与工具（**3-4 天**）

**目标**：补齐 Google / Mistral，工具补齐 grep / find / ls / web search（可选）。

**交付**：
- [ ] `GoogleGeminiProvider`（HTTP + SSE）
- [ ] `MistralProvider`（复用 OpenAI）
- [ ] `grep` 工具（自实现简化 ripgrep）
- [ ] `find` / `ls` 工具
- [ ] `web search` 工具（可选）

---

### Phase 7 — 优化与打包（**3-5 天**）

**目标**：性能、可移植性、文档。

**交付**：
- [ ] 性能 pass（启动 < 200ms、按键 < 16ms）
- [ ] macOS arm64 构建验证
- [ ] Linux aarch64 交叉编译验证
- [ ] K3 riscv64 验证（既然已经在 K3 上跑）
- [ ] `tools/release.sh`：tar.gz + .deb
- [ ] README + BUILD.md + CHANGELOG
- [ ] 安装到 `/usr/local/bin/pi`
- [ ] 单测覆盖 ≥ 60%

---

## 4. 阶段时间预算

| 阶段 | 工作日 | 累计 |
|---|---:|---:|
| P0 基础 | 1-2 | 2 |
| P1 pi_ai + Anthropic | 3-5 | 7 |
| P2 pi_agent + 工具 | 3-4 | 11 |
| P3 TUI | 5-7 | 18 |
| P4 pi_coding | 4-5 | 23 |
| P5 模式 + OAuth + Compaction | 5-7 | 30 |
| P6 余 Provider + 工具 | 3-4 | 34 |
| P7 优化 + 打包 | 3-5 | 39 |

约 **6-8 周** 一人全职完成 V1。本机 K3 上验证。

## 5. 立即行动（本会话）

1. 写 `240-Implementation-Plan.md`（本文档）
2. 创建仓库骨架
3. Vendor `nlohmann/json.hpp` + `doctest.h`
4. 实现 `pi_core` 全部 Phase 0 模块
5. 实现 `pi_http` Phase 1 核心（HttpClient + SseParser）
6. 实现 `pi_ai` Phase 1 核心（EventStream + Anthropic Provider + OpenAI Provider）
7. `apps/pi/main.cpp` 解析 `-p` 调用 streamSimple 流到 stdout
8. 验证：build + ctest + `pi -p "say ok"`（需 ANTHROPIC_API_KEY）
9. 提交 + 推送

## 6. 风险与回退

| 风险 | 触发条件 | 回退方案 |
|---|---|---|
| OpenSSL BIO 实现 HTTP 太慢 / 有 bug | Phase 1 验证不过 | 让用户 sudo 装 `libcurl4-openssl-dev`，换 libcurl |
| Anthropic SSE 协议细节变化 | 测试失败 | 锁定协议版本号，加适配层 |
| TUI 终端协议碎片化 | Phase 3 验收不过 | 退到"无 TUI，仅 Print + RPC"也能用 |
| Compaction 上下文估算误差大 | Phase 5 验收不过 | 直接调真实 LLM API 拿 token 数 |
| 一阶段超时 | 任何阶段 > 预算 2 倍 | 砍掉后续阶段，先发 V0.5 |

## 7. 与上游对账

每隔 1-2 周跑一次：

```bash
# 把上游作为 submodule 或临时 clone
git clone --depth 1 https://github.com/earendil-works/pi.git upstream
# diff: 我们已实现的能力 vs 上游 0.79.4
./tools/upstream-diff.sh
```

输出格式：哪些功能我们有了、哪些没、优先级建议。
