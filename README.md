# prime-agent

`prime-agent` 是 [`pi`](https://github.com/earendil-works/pi) 的 **C/C++ 重实现版本**，
目标是产出一个单一可执行 `pi`，可在 Linux（含 K3 RISC-V）/ macOS / Windows 上运行，
对接 Anthropic / OpenAI / Google 等 LLM 提供商。

## 状态

| 项 | 状态 |
|---|---|
| 阶段 | **Phase 0（基础与骨架）** |
| 版本 | `0.1.0` |
| 平台 | Linux x86_64 / aarch64 / **riscv64 (K3)** / macOS arm64 |
| 编译器 | `g++ 15+` / `clang++ 17+`，C++20 |
| 构建 | CMake ≥ 3.20 |

当前已完成：

- [x] 仓库骨架（`libs/`, `apps/`, `tests/`, `third_party/`, `tools/`）
- [x] `pi_core` 基础库（Result、Error、JSON、日志、路径、字符串、ANSI、文件 I/O、flock、Unicode 宽度）
- [x] `pi_http` 库（HttpClient + SseParser，基于 OpenSSL BIO）
- [x] `pi_ai` 库（EventStream + Anthropic + OpenAI Chat Providers）
- [x] `pi` CLI 解析 `-p`、`--version`、`--help`、`--list-models`
- [x] doctest 单测框架
- [x] 烟测脚本 `tools/smoke_pi.sh`

后续阶段见 [`pi-spec/240-Implementation-Plan.md`](pi-spec/240-Implementation-Plan.md)。

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 运行
./build/apps/pi/pi --version
./build/apps/pi/pi --help

# 单测
ctest --test-dir build --output-on-failure

# 烟测（需 ANTHROPIC_API_KEY）
export ANTHROPIC_API_KEY=sk-ant-...
./tools/smoke_pi.sh
```

## 目录

```
apps/pi/         可执行入口
libs/core/       pi_core: 基础设施（无外部依赖，除 nlohmann/json + OpenSSL）
libs/http/       pi_http: HTTP/TLS 客户端
libs/ai/         pi_ai: LLM Providers
libs/agent/      pi_agent: Agent 循环（占位，Phase 2 填充）
libs/tui/        pi_tui: TUI（占位，Phase 3 填充）
libs/coding/     pi_coding: CLI 装配（占位，Phase 4 填充）
third_party/     Vendored 单文件依赖
tests/           跨模块单测
tools/           开发辅助脚本
pi-spec/         设计规范（230+240 已写，10-200 见其他文档）
```

## 许可证

MIT（与上游 `pi` 一致）。Vendored 依赖保留各自许可证。
