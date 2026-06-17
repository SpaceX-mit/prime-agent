# 60 · 验收标准 (Acceptance Criteria)

> 每个子系统必须能通过的硬性指标。可作为回归测试目标。条目按子系统分组。

## 0. 全局 / 构建

- [ ] `npm run check` 在干净 checkout 上零 error / warning / info
- [ ] `scripts/build-npm.sh` 在 Linux x86_64 上生成 4 个 tarball，文件大小与 [02-Build-and-Release §3](./02-Build-and-Release.md) 一致 ±10%
- [ ] 同一脚本生成的 `pi-coding-agent-0.79.4.tgz` 在 K3 riscv64 (Node 22.22) 上可 `npm install --omit=dev`，`pi --version` 输出 `0.79.4`
- [ ] `pi -p "Say exactly: ok"` 在无 auth 的最小化环境上输出引导（不是 crash）
- [ ] `pi --list-models` 至少列出 30+ 个 provider

## 1. `@earendil-works/pi-ai`

- [ ] 任意内置 provider 的 `streamSimple` **绝不抛 / 绝不 reject**；错误落 `stopReason: "error"`
- [ ] `MODELS` 至少包含 30 个 provider 的所有 model id
- [ ] `validateToolArguments(tool, args)` 对合法 args 通过、对非法 args 返回带 `errorMessage` 的拒绝
- [ ] `OAuthProvider` 接口在 Claude / Codex / Copilot 三个内置流程中能走通 device code / browser callback
- [ ] `cleanupSessionResources` 调用后，bash 的 `fullOutputPath` 临时文件被删除
- [ ] `isContextOverflow` 至少识别 Anthropic / OpenAI 两种典型 overflow 错误
- [ ] `clampThinkingLevel` 对不支持的 level 返回该 model 支持的最高 level

## 2. `@earendil-works/pi-agent-core`

- [ ] `agentLoop` 在 `signal.aborted` 时立即发出 `agent_end` 并把 `stopReason: "aborted"`
- [ ] `agentLoopContinue` 在最后一条是 assistant 时**抛错**（这是契约）
- [ ] `convertToLlm` 抛错时主循环不进入正常事件序列（这是契约）
- [ ] `beforeToolCall` 返回 `{ block: true }` 时产生 `error` tool result，循环继续
- [ ] `shouldCompact` 在 token > `reserve + keepRecent` 时返回 true
- [ ] `compact` 写入 session 后 reload，能看到新的 `compaction` entry
- [ ] `generateBranchSummary` 给定 entry 列表，能产出 `{summary, cancelled}` 或类似结果
- [ ] 并发模式（`ToolExecutionMode = "parallel"`）下，`tool_execution_end` 事件**按完成顺序**发，tool-result 消息**按 assistant 源顺序**写

## 3. `@earendil-works/pi-tui`

- [ ] `TUI` 在 80×24 终端上启动 < 100ms（冷启动）
- [ ] 任何 `Component.render(width)` 对同一输入必须返回相同输出（纯函数）
- [ ] `KeybindingsManager` 默认 keymap 与 `docs/keybindings.md` 一致
- [ ] Kitty 键盘协议启用后，鼠标选择不破坏 TUI（不写入多余字符）
- [ ] Kitty image 协议下，图片渲染不阻塞 > 16ms/帧
- [ ] 退出 TUI 时 `showCursor()` 被恢复（`echo $?` 后光标可见）
- [ ] `process.exit` 后终端不留乱码（用 `script` 录屏检查）

## 4. `@earendil-works/pi-coding-agent`

### 4.1 CLI 解析

- [ ] `pi --help` 输出 Usage
- [ ] `pi --version` 输出 `0.79.4`
- [ ] `pi --list-models` 列出所有 provider / model
- [ ] 未知 flag 进入 `unknownFlags` map，不报错
- [ ] `--thinking xhigh` 对不支持的 model 降级并 warn
- [ ] `pi @README.md "..."` 内联文件内容

### 4.2 三种模式

- [ ] TTY 启动 → 交互模式
- [ ] `pi -p "..."` → print 模式
- [ ] `pi --mode json "..."` → JSON 事件流
- [ ] `pi --mode rpc` → RPC 模式，stdin 一行 JSON 命令触发响应
- [ ] `pi --export <path>` 把现有 session 导出为可打开的 HTML

### 4.3 内置工具

- [ ] `bash` 超时后子进程被 kill（不留 zombie）
- [ ] `bash` detach 进程在 SIGTERM 时被清理（`killTrackedDetachedChildren`）
- [ ] `read` 自动检测 PNG / JPEG / GIF，输出 image content
- [ ] `edit` 多个 `oldString` 一次替换（multi-edit）
- [ ] `grep` 在大目录（> 10k 文件）下响应 < 1s
- [ ] `find` 与 `.gitignore` 兼容
- [ ] 所有工具对路径穿越（`../` 逃出 cwd）一律拒绝

### 4.4 Session 管理

- [ ] 每个 user / assistant / tool 消息在 prompt 结束后立即落盘
- [ ] JSONL 文件可被 `cat` 查看，且每行合法 JSON
- [ ] `pi -c` 续最近 session
- [ ] `pi -r` 弹 fuzzy 搜索 picker
- [ ] `pi --session <id>` 用 8 字符 prefix 匹配也能找到
- [ ] `/fork <entryId>` 后新 session 独立 ID、可独立 `-c` / `--resume`
- [ ] `/tree` 显示 entry 树，方向键导航

### 4.5 Compaction

- [ ] 大上下文（> 100k tokens）下 `pi -p "..."` 自动压完再答
- [ ] `/compact` 手动触发后，session 出现新 `compaction` entry
- [ ] 压后再 `/resume` 加载能继续对话
- [ ] 压后保留文件操作列表（read/modified）

### 4.6 扩展

- [ ] `~/.pi/agent/extensions/foo.ts` 自动加载
- [ ] 扩展抛错不影响主循环
- [ ] `/reload` 热重载扩展
- [ ] `ctx.ui.custom<T>(factory)` 能注入自定义 TUI 组件
- [ ] `ctx.ui.setWidget(c)` 在 editor 下方挂 widget
- [ ] 扩展的 `pi.appendEntry` 写出的 `custom_message` 在 `/tree` 中可见

### 4.7 鉴权

- [ ] `ANTHROPIC_API_KEY=... pi` 直接进入交互
- [ ] `/login` Claude 走 OAuth 流程
- [ ] `auth.json` 写盘有 `proper-lockfile` 保护（并发写不损坏）
- [ ] `--api-key` 不入磁盘（重启后需重新指定）

### 4.8 设置 / 资源

- [ ] `.pi/settings.json` 覆盖 `~/.pi/agent/settings.json`
- [ ] `.pi/extensions/` 加载且 `~/.pi/agent/extensions/` 仍在
- [ ] 主题切换实时生效
- [ ] `--no-skills` 后 system prompt 不含 skill 块

### 4.9 RPC 模式

- [ ] `pi --mode rpc` 启动后第一行 JSON 命令立刻得到 response
- [ ] 长跑 prompt 时事件持续流出 stdout（行分隔 JSONL）
- [ ] 客户端 `abort` 命令立即取消进行中的 prompt
- [ ] 扩展 UI request / response 双向流通

### 4.10 Print 模式

- [ ] `pi -p "..."` 只输出最终 AssistantMessage 的 text
- [ ] `pi --mode json "..."` 输出包含 start / delta / end / done 的完整事件序列
- [ ] 多个 message 用换行分隔能串成多次 prompt

### 4.11 HTML 导出

- [ ] `pi --export out.html session.jsonl` 生成可在浏览器打开的离线 HTML
- [ ] HTML 内嵌主题色与 syntax highlight
- [ ] 扩展工具的 renderer 在 HTML 中正确呈现

## 5. 依赖治理

- [ ] `npm run check:pinned-deps` 对任何 `^x.y.z` / `~x.y.z` 形式报错
- [ ] `npm run check:shrinkwrap` 在 `package-lock.json` 与 `packages/coding-agent/npm-shrinkwrap.json` 不一致时报错
- [ ] 直连 dep 都出现在 `package.json` 的精确版本字段
- [ ] `npm run check:ts-imports` 在跨包相对导入时报错
- [ ] `pre-commit` hook 在 lockfile 改动未带 `PI_ALLOW_LOCKFILE_CHANGE=1` 时阻止提交

## 6. K3 RISC-V 部署（特定场景）

- [ ] 一次完整 `./scripts/build-npm.sh --smoke-test` 跑通
- [ ] 4 个 tarball 在 K3 上 `npm install --omit=dev` 成功（不需要联网）
- [ ] `pi --version` / `pi --list-models` / `pi -p "Say exactly: ok"` 在 K3 上结果与 x86_64 一致
- [ ] TUI 启动不依赖 `gcc` / `python` / `make`
- [ ] `photon_rs_bg.wasm` 不丢失（图像处理可用）

## 7. 安全 / 隔离

- [ ] pi **不内置**权限系统（明确写在 README 中）
- [ ] `examples/extensions/gondolin` 演示在本地 micro-VM 跑工具
- [ ] `examples/extensions/sandbox` 演示 Anthropic sandbox
- [ ] `docs/containerization.md` 描述三种沙箱模式
- [ ] 默认无写入 `~/.ssh/`、`/etc/`、`.env` 的隐式路径

## 8. 性能预算（粗略）

- [ ] TUI 冷启动到首帧 < 200ms
- [ ] 编辑器按键到重绘 < 16ms
- [ ] tool call（bash）开始到首 chunk < 50ms
- [ ] session 加载（10k entry）< 500ms
- [ ] compaction（100k token → summary）< 30s（取决于 model）

## 9. 可观察性

- [ ] 调试日志写到 `~/.pi/agent/debug.log`（可配置路径）
- [ ] 关键事件（compaction、retry、session_switch）有结构化日志
- [ ] auto retry 在 TUI footer 有可见指示
