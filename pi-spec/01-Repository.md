# 01 · 仓库概览 (Repository)

> 仓库 = `pi-mono` monorepo。最终交付物是 `@earendil-works/pi-coding-agent` 包内的 `pi` CLI。

## 1. 基本信息

| 项 | 值 |
| --- | --- |
| 仓库名 | `pi-mono` |
| 上游组织 | `earendil-works`（GitHub） |
| 内部克隆 | `git@gitlab.dc.com:bianbu/ai/pi-spacemit.git`（见 `BUILD.md`） |
| 当前版本 | `0.79.4`（来自 `packages/coding-agent/package.json`） |
| 许可证 | MIT（`LICENSE`） |
| Node 要求 | `>= 22.19.0`（来自根 `package.json` 的 `engines`） |
| 语言 | TypeScript (ESM, `erasableSyntaxOnly` 严格模式) |
| 构建产物 | 纯 JS + 唯一 WASM (`@silvia-odwyer/photon-node`) |

## 2. 顶层目录

```
pi/
├── AGENTS.md              # 人类与 AI 代理的协作规约（先读）
├── README.md              # 项目对外简介
├── BUILD.md               # K3 RISC-V 上从源码构建/打包的内部指南
├── CONTRIBUTING.md        # 贡献者门槛（lgtm/auto-close 流程）
├── SECURITY.md            # 漏洞披露策略
├── package.json           # monorepo 根（workspaces、scripts、devDeps）
├── tsconfig.base.json     # 跨包共享的 TS 配置
├── tsconfig.json
├── biome.json             # lint + format 配置
├── package-lock.json      # 唯一依赖真理来源
├── pi-test.sh / .ps1 / .bat  # 源码直跑脚本
├── test.sh                # 测试入口
├── scripts/               # 构建/检查/发布脚本
├── .pi/                   # 项目内 pi 资源（可能含 skills/themes）
├── .github/workflows/     # CI 工作流
├── .husky/                # git hooks
├── docs/                  # （如存在）跨包文档
├── node_modules/          # 工作区依赖
└── packages/
    ├── ai/                # @earendil-works/pi-ai
    ├── agent/             # @earendil-works/pi-agent-core
    ├── tui/               # @earendil-works/pi-tui
    └── coding-agent/      # @earendil-works/pi-coding-agent
```

## 3. 四个发布包

| 包 | 目录 | 角色 | 关键导出 |
| --- | --- | --- | --- |
| `@earendil-works/pi-ai` | `packages/ai/` | 多 Provider 统一 LLM API | `streamSimple`, `completeSimple`, `getModel`, `getProviders`, `MODELS`, 全部内置 provider |
| `@earendil-works/pi-agent-core` | `packages/agent/` | Agent 循环 + 工具注册 + 状态 | `agentLoop`, `Agent`, `agent-loop`, harness 全部 |
| `@earendil-works/pi-tui` | `packages/tui/` | 差分渲染 TUI 组件 | `TUI`, `Box`, `Text`, `Editor`, `Input`, `SelectList`, `Markdown`, `ProcessTerminal` |
| `@earendil-works/pi-coding-agent` | `packages/coding-agent/` | `pi` CLI + Session + 扩展 | `AgentSession`, `createAgentSession`, 全部工具/扩展类型 |

> **包间依赖方向**（严格自下而上）：
>
> ```
> pi-coding-agent ──▶ pi-agent-core ──▶ pi-ai
>        │                │
>        └────▶ pi-tui ◀──┘    (tui 被 coding-agent 直接使用)
> ```
>
> `pi-tui` 与 `pi-agent-core` 之间没有直接依赖。`pi-ai` 不依赖任何其他 pi 包。

## 4. 代码统计（粗略）

| 包 | `src/` 文件数 | 主要源目录 |
| --- | --- | --- |
| `ai` | ~35 | `providers/`, `utils/`, `utils/oauth/`, `providers/images/` |
| `agent` | ~15 | `harness/`, `harness/compaction/`, `harness/session/`, `harness/env/` |
| `tui` | ~20 | `components/`, `utils.ts`, `terminal.ts`, `keys.ts`, `keybindings.ts` |
| `coding-agent` | ~110 | `core/`, `core/tools/`, `core/extensions/`, `modes/`, `modes/interactive/`, `cli/` |
| 测试 | 100+ | `coding-agent/test/` 为主 |

## 5. 核心工作流

```
开发者改代码
   │  npm run check  (lint+tsgo+pinned-deps+shrinkwrap+ts-imports+browser-smoke)
   ▼
发版  npm run release:patch   ──▶  升 4 个包的 version 号
                                   ──▶  生成 CHANGELOG
                                   ──▶  提交 Release vX.Y.Z
                                   ──▶  push tag vX.Y.Z
                                   ▼
                            GitHub Actions build-binaries.yml
                                   ▼
                            npm OIDC 信任发布（不需要 OTP）
```

详细构建脚本见 `02-Build-and-Release.md`。

## 6. 上游常用入口

- 项目站：<https://pi.dev>
- 文档：<https://pi.dev/docs/latest>
- Discord：<https://discord.com/invite/3cU7Bz4UPx>
- RFC 索引：<https://rfc.earendil.com/keyword/pi/>
- 相关项目：`earendil-works/pi-chat`（Slack/聊天自动化）

## 7. 与本仓库直接相关的内部约定

阅读本仓库前必读 `AGENTS.md`。关键规约：

- 直连 dep 必须 pin 到精确版本（`save-exact=true`）
- 不能修改 `packages/ai/src/models.generated.ts`（应改 `packages/ai/scripts/generate-models.ts` 后再生成）
- 不能在 `packages/*/src` 使用 `enum` / `namespace` / 参数属性（`erasableSyntaxOnly`）
- 不能把 `key` 写死为 `matchesKey(keyData, "ctrl+x")` 这种硬编码字符串
- 提交用 `git add <path>` 显式列文件；禁止 `git add -A`
- 改完代码必须 `npm run check` 全过
