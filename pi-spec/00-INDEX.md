# 00 · 文档总入口

> pi-mono 仓库的整理式规格说明。文档按**先手优先级**编号：编号越小越先读，概览在前，实现细节在中，验收标准殿后。

## 阅读顺序

| 编号 | 类别 | 文档 | 一句话定位 |
| --- | --- | --- | --- |
| 01 | 仓库 | [Repository](./01-Repository.md) | 这个仓库是什么、由谁维护、装在哪 |
| 02 | 仓库 | [Build-and-Release](./02-Build-and-Release.md) | 怎么构建、怎么发版、依赖治理规则 |
| 10 | 模块 | [Module-pi-ai](./10-Module-pi-ai.md) | LLM Provider 统一抽象与流式协议 |
| 20 | 模块 | [Module-pi-agent-core](./20-Module-pi-agent-core.md) | Agent 循环、状态、上下文、工具、Compaction |
| 30 | 模块 | [Module-pi-tui](./30-Module-pi-tui.md) | 差分渲染终端 UI 组件库 |
| 40 | 模块 | [Module-pi-coding-agent](./40-Module-pi-coding-agent.md) | `pi` CLI：交互 / Print / RPC 三种运行模式 |
| 50 | 功能 | [Features](./50-Features.md) | 端到端可观察的功能清单（按子系统分组） |
| 60 | 验收 | [Acceptance-Criteria](./60-Acceptance-Criteria.md) | 每个子系统需要被验证的硬性指标 |
| 70 | 架构 | [Architecture](./70-Architecture.md) | 模块级别架构图（9 张 Mermaid 图） |
| 80 | 架构 | [Sequences](./80-Sequences.md) | 模块间时序图（10 个核心流程） |
| 90 | 接口 | [Interfaces](./90-Interfaces.md) | 跨模块关键接口的完整 TS 签名 |

## 仓库一句话定义

`pi-mono` 是一个 **monorepo**，由 4 个可独立发布的 npm 包组成，最终产物是名为 `pi` 的自扩展 CLI 编码代理。

```
@earendil-works/pi-coding-agent   ← 终端用户实际运行的 `pi` 命令
        │
        ├── @earendil-works/pi-agent-core   (Agent 循环 + 状态)
        │         │
        │         └── @earendil-works/pi-ai  (多 Provider LLM 统一 API)
        │
        └── @earendil-works/pi-tui          (终端 UI 组件库)
```

## 文档约定

- 文档中所有 `路径` 引用**相对当前仓库根** `/home/bianbu/aiws/pi/`。
- 所有 `pi <sub-command>` 形式的命令来自 `@earendil-works/pi-coding-agent` 暴露的 `pi` 可执行文件。
- 文档中所有版本号基于当前 `package.json`：`0.79.4`。
- 凡是引用 `path/to/x.ts` 形式的源码定位，均在当前仓库内可直接打开。
