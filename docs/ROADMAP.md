# prime-agent 路线图：复刻并超越上游 pi

> **目的**：把 prime-agent 推到商用标准、客户最优体验。本路线图承接已完成的 P0（TUI 重写）+ P1（可靠性），规划 P2（功能差距）+ P3（生态/跨平台）。
> **数据源**：`docs/FEATURE-MAP.md`、`docs/SYSTEMS-STATUS.md`（与上游 `pi` 的逐项对照）。

## 进度总览

| 阶段 | 内容 | 状态 |
|---|---|---|
| **P0** | TUI 重写：主屏直写 + DECSTBM 锁底 + scrollback + 固定 footer（spinner/token）+ thinking 色 | ✅ 已完成（见 `docs/TUI-REWRITE.md`） |
| **P1** | 可靠性：重试 + 指数退避 + 错误人话化 + socket 兜底 | ✅ 已完成 |
| **P2** | 高频功能差距补齐 | ⏳ 规划中 |
| **P3** | 生态与跨平台 | ⏳ 规划中 |

整体规模对比上游（来自 SYSTEMS-STATUS）：源码约上游 11%，模型 21/975，provider 5/9，slash 13/23，测试 13 套件。核心工作流（print/interactive/rpc 三模式 + 7 工具 + 流式）可用；差距集中在生态、会话编辑、context 注入。

---

## P2：高频功能差距（按客户价值排序）

### P2.1 会话续接真正可用（高 · 当前是 stub）
- `-c/--continue`、`-r/--resume`、`--session <id>` 命令行参数已存在但加载逻辑是 stub（FEATURE-MAP §4/§15）。
- `/resume <id>` / `/tree` slash command 仍是占位（§16）。
- 交互模式内 `/resume`/`/continue` 已实现，复用其 `build_session_context` 路径打通 CLI 入口即可。
- **交付**：CLI 启动即加载目标会话；`-r` 提供 fuzzy 选择 UI（现 main.cpp `pick_session` 已有雏形但未接）。

### P2.2 Context 文件注入（高 · 上游核心体验）
- 上游加载 `~/.pi/agent/AGENTS.md` + 项目级 `AGENTS.md`/`CLAUDE.md`，向上查找 cwd，注入 system prompt（§10）。本项目 ❌。
- **交付**：cwd 向上查找 + 合并 + `--no-context-files` 开关。

### P2.3 Skills 系统（高 · 上游 70% 用户价值之一）
- 加载 `~/.pi/agent/skills/`、frontmatter 解析、system prompt 注入、`/skill-name` 触发、`/skills` 列表（§6）。本项目 ❌。
- **交付**：scoped 加载（user/project）+ frontmatter 元数据 + `--skills/--no-skills`。

### P2.4 自动 compaction（中 · 长会话稳定性）
- 手动 `/compact` 已可用；自动阈值触发 + context overflow 检测未接到 agent loop（§5、SYSTEMS-STATUS §1.2）。
- **交付**：`isContextOverflow` 检测 + 阈值触发自动 compact，复用现有 `compact()`。

### P2.5 模型切换 UX（中）
- `/model <id>` 是占位（§16）；`--models` scoped + Ctrl+P 循环未实现（§15）。
- **交付**：`/model` 真正切换运行时模型；scoped 模型集 + 快捷键循环。

### P2.6 Markdown 渲染（中 · 显示质量）
- 上游用 md4c 渲染助手输出；本项目纯文本（§11）。
- **交付**：轻量 markdown → ANSI（标题/列表/代码块/粗体），或 vendor md4c。主屏直写架构下逐行 emit 即可。

---

## P3：生态与跨平台

### P3.1 更多 Provider（中）
缺失（FEATURE-MAP §2、SYSTEMS-STATUS §1.2）：
- OpenAI Responses / Codex Responses（ChatGPT 订阅用户）
- Azure OpenAI、AWS Bedrock Converse、Google Vertex、Cloudflare AI Gateway
- OAuth providers（Codex / Copilot / Claude.ai Pro/Max）
- Anthropic prompt cache retention（1h/short/none）

### P3.2 Extension 系统（中 · 上游最大差异面）
- 上游 1600+ 行 ExtensionAPI、25+ 事件、`registerTool`/`registerCommand`/`registerProvider`、UI 桥、热重载、70+ 示例扩展（§9）。
- C++ 无 TS runtime → 需另设机制（如子进程 + JSON 协议，参考上游 subagent extension 模式），或暴露稳定 C ABI。**重大设计决策，需单独立项。**

### P3.3 项目信任 / 权限（中 · 安全）
- 首次交互确认、`trusted-projects.json`、检测 trust-requiring 资源（§19）。本项目 ❌。
- **交付**：写文件/跑 shell 前的信任门（非 yolo 模式），与 P3.2 扩展执行联动。

### P3.4 Web 工具（低）
- `web_search` / `fetch` 工具（§3）。本项目 ❌。

### P3.5 跨平台（低 · 取决于目标客户）
- Windows x64 端口（§20，当前 ❌）。
- macOS arm64 验证（应能编译，未跑过）。
- CI：x86_64/aarch64/riscv64 GitHub Actions（§23）。

### P3.6 MCP（低 · 观望）
- 上游协议尚未稳定，暂缓。

---

## 明确的非目标（V1）

- Codex WebSocket 私有协议
- 浏览器模式 / browser-eval（C++ 跑浏览器不合理）
- Telemetry

---

## 建议执行顺序

1. **P2.1 会话续接** — 改动小、客户立刻可感、stub 已有半成品。
2. **P2.2 Context 文件** — 中等改动、上游核心体验、无外部依赖。
3. **P2.4 自动 compaction** — 复用现有 `compact()`，长会话不崩。
4. **P2.6 Markdown 渲染** — 显示质量，主屏直写架构契合。
5. **P2.3 Skills** — 体量较大，独立推进。
6. **P3.x** — 按目标客户群定优先级（订阅登录 → provider；企业 → 信任/权限；插件生态 → extension）。
