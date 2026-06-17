# 100 · 子系统功能索引 (Subsystem Features Index)

> 按"子系统 = 4 个 npm 包"逐个梳理功能。每个子系统一份独立文档：
>
> - [110-Subsystem-pi-ai.md](./110-Subsystem-pi-ai.md) — `@earendil-works/pi-ai`
> - [120-Subsystem-pi-agent-core.md](./120-Subsystem-pi-agent-core.md) — `@earendil-works/pi-agent-core`
> - [130-Subsystem-pi-tui.md](./130-Subsystem-pi-tui.md) — `@earendil-works/pi-tui`
> - [140-Subsystem-pi-coding-agent.md](./140-Subsystem-pi-coding-agent.md) — `@earendil-works/pi-coding-agent`

## 1. 四子系统矩阵

| 子系统 | 一句话 | 谁在用 | 关键文档 |
| --- | --- | --- | --- |
| `pi-ai` | 多 Provider LLM 统一 API | `agent-core` / `coding-agent` / 外部 SDK | [110](./110-Subsystem-pi-ai.md) |
| `pi-agent-core` | Agent 循环 + 状态机 + 工具调度 | `coding-agent` / 外部 SDK | [120](./120-Subsystem-pi-agent-core.md) |
| `pi-tui` | 差分渲染 TUI 组件库 | `coding-agent` / 外部应用 | [130](./130-Subsystem-pi-tui.md) |
| `pi-coding-agent` | `pi` CLI + Session + 扩展系统 | 终端用户 / IDE / CI | [140](./140-Subsystem-pi-coding-agent.md) |

## 2. 文档结构

每个子系统文档统一分 6 节：

1. **角色定位** — 一句话 + 在仓库中的位置
2. **入口与导出面** — `index.ts` 主要 re-export
3. **功能分组** — 按职责领域列能力（每条带源码路径）
4. **典型用法** — 1~3 个最常用 API 示例
5. **不导出但可调用的内部** — 标"internal"的能力
6. **不变量 / 契约** — 实现或调用时必须遵守

## 3. 横向能力对照

| 能力 | `pi-ai` | `pi-agent-core` | `pi-tui` | `pi-coding-agent` |
| --- | --- | --- | --- | --- |
| 流式 LLM 调用 | ✅ 核心 | 用 | — | 用 |
| Agent 循环 | — | ✅ 核心 | — | 用 + 包装 |
| 工具抽象 | ✅ Tool schema | ✅ AgentTool | — | ✅ 内置 7 个 + 扩展点 |
| 鉴权 | ✅ OAuth + env | — | — | ✅ 持久化 |
| 上下文压缩 | — | ✅ Compaction | — | ✅ 触发器 + 集成 |
| 终端渲染 | — | — | ✅ 核心 | ✅ 交互模式 |
| Session 持久化 | — | ✅ Repo 抽象 | — | ✅ JSONL 实现 |
| 扩展系统 | — | — | — | ✅ 核心 |
| 导出 HTML | — | — | — | ✅ |
| RPC 模式 | — | — | — | ✅ |
| SDK 暴露 | ✅ 全部 | ✅ 全部 | ✅ 全部 | ✅ SDK 模块 |

## 4. 顺读建议

先读 `110`（最底层），再 `120`（用 `110`），再 `130`（独立），最后 `140`（用前三者）。这是依赖方向。
