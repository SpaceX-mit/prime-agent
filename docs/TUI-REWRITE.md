# TUI 重写：主屏直写 + DECSTBM 锁底（P0）+ 可靠性加固（P1）+ 视觉对齐上游（P2-vis）

> 状态：**已实现并验证**（构建 0 新增 warning，14/14 ctest 通过，PTY 自动化 10/10 通过）。
> 基线 commit：`1523293`（重写前的全屏 TUI 旧版）。


## 1. 背景：为什么重写

`prime-agent` 是上游 `pi`（TypeScript）的 C++ 复刻，目标按商用标准做客户最优体验。重写前的交互模式是 **"GUI 流亡者"式全屏 TUI**：

- `terminal.cpp` 进 alt-screen（`\x1b[?1049h`）+ 三种 mouse 模式（`?1000h/?1003h/?1006h`）。
- `interactive.cpp`（1037 行）自管 `scroll_back`/`follow`/`tui.render()` 全屏重绘。

后果（客户实际会骂的点）：

1. mouse mode 抢走滚轮 → **鼠标轮无法向上翻历史 chat**。
2. alt-screen 让 chat 不进终端 scrollback → **退出后聊天全部消失**，也无法鼠标选中复制。
3. 全屏重绘反复擦写 → **闪烁**。
4. 自造滚动/换行/视口轮子 → 代码多、易错、与终端打架。

## 2. 架构转变：well-behaved CLI

参考 Claude Code / aider / fabric / sgpt 的做法——**别跟终端打架，让终端干它最擅长的事**。

| | 旧（全屏自绘） | 新（主屏直写 + DECSTBM） |
|---|---|---|
| chat 输出 | 写进自管 buffer，全屏 repaint | `ui.emit()` 直写 stdout 滚动区 |
| 滚动 / 历史 | 自管 `scroll_back`/`follow` | 终端原生 scrollback（滚轮天然工作） |
| 换行 | 自实现 soft-wrap | 终端原生 wrap |
| 选中复制 | 不支持 | 终端原生支持 |
| 退出后 | 聊天消失 | 聊天留在终端缓冲 |
| 底部 UI | 全屏树的两行 | DECSTBM 锁底 2 行，绝对定位重画 |

### 屏幕布局

```
┌────────────────────────────────────┐
│ row 1 .. rows-2   滚动区(chat)       │  ← 进终端 scrollback，滚轮可翻
│   ...                               │
├────────────────────────────────────┤
│ row rows-1        输入行(prompt+text)│  ← DECSTBM 区外，固定
│ row rows          footer(状态栏)     │  ← model · spinner Ns · in/out tokens
└────────────────────────────────────┘
```

## 3. 实现要点

### 3.1 终端控制（`libs/tui/src/terminal.cpp`）

- `enter_raw_mode()`：删 alt-screen / clear / 三个 mouse 模式。**保留** bracketed paste（`?2004h`）+ Kitty CSI-u（`>1u`）+ hide cursor（输入行自绘反色光标，避免双光标，见 5e5781b）。
- `leave_raw_mode()`：加 `\x1b[r`（清 DECSTBM）+ Kitty pop（`<u`）+ 关 bracketed paste + show cursor + RESET。
- `reset_terminal_quickly()`（崩溃/信号路径）：写 `\x1b[r\x1b[?2004l\x1b[?25h\x1b[0m`。新增挂到 **SIGTERM/SIGHUP** handler（恢复终端后用默认 disposition 重新 raise）；SIGINT 仍走 raw-mode 的 Ctrl-C 两段式。
- 删 SGR mouse → WheelUp/WheelDown 的产生分支（mouse 模式已关，正常不会再收到；若收到则吞掉，不泄漏到输入）。

### 3.2 ANSI helper（`libs/core/include/pi_core/ansi.hpp`）

```cpp
set_scroll_region(top, bottom)  // DECSTBM  \x1b[<top>;<bottom>r
reset_scroll_region()           // \x1b[r
save_cursor()                   // DECSC  ESC 7  （存位置+SGR+滚动区）
restore_cursor()                // DECRC  ESC 8
```

**关键决策**：用 `ESC 7/ESC 8`（DECSC/DECRC），**不用** `\x1b[s/\x1b[u`（SCOSC/SCORC）——后者各终端实现不一，且不保存 SGR 属性。

### 3.3 渲染序列化（`libs/tui/src/modes/interactive.cpp` 的 `Ui` 类）

单个 `std::mutex` 串行化所有 stdout 写，主线程键盘重画与 agent 线程流式 emit 不会交错。三个原语：

- `setup()`：清屏 → `set_scroll_region(1, rows-2)` → 光标停滚动区底 → `save_cursor`。启动 + resize + `/clear` + resume 时调用。
- `emit(text)`：`restore_cursor`（回滚动区）→ 写 text（自然滚动进 scrollback）→ `save_cursor` → 重画底部。
- `redraw_bottom()`：绝对定位 row `rows-1`（输入行，复用 `components::Input::render`）+ row `rows`（footer），各 `\x1b[2K` 清行后重画，最后 `restore_cursor` 把光标弹回滚动区。

### 3.4 主循环

单线程 50ms poll（`term.try_read_key(50)`）：

- **resize**：SIGWINCH handler 是 no-op，主循环每 tick 比较 `term.size()`，变化则重建滚动区（不在信号上下文写 stdout）。
- **spinner**：流式时每 ~200ms 刷 footer（`⠋⠙⠹…` + 秒数 + token 数）。
- **Ctrl-C 两段式**：1 次 abort 当前 run，2s 内 2 次强退。
- **流式期间允许打字进 buffer**（不硬禁键），仅 Enter 提交时给"agent running"提示并丢弃（V1 语义）。

### 3.5 降级与清理

- **rows < 5**（`rows-2 <= 1`）：fallback 无锁底模式——不设 DECSTBM，输入行 `\r` 原地重画，footer 省略。
- **非 TTY**：interactive 直接拒绝（要 TTY）；管道场景走 print 模式 `-p`（main.cpp 已支持 `prompt == "-"` 读 stdin）。
- **退出清理**：正常退出 `ui.teardown()` + `leave_raw_mode()`；SIGTERM/SIGHUP 走 `reset_terminal_quickly`；都能复原终端。

## 4. 保留的旧功能（无回归）

13 slash commands（`/exit /quit /clear /help /new /model /login /compact /history /sessions /resume /continue /tree`）、multi-turn history、resume from JSONL、LLM compaction、Ctrl-C 两段式、INC-006 错误红字、ThinkFilter（`<think>` 标签）、UTF-8 硬化（bracketed paste + Kitty）、JSONL 实时落盘、双光标修复——逻辑全部搬运到新渲染框架。

## 5. 可靠性加固（P1）

### 5.1 重试 + 指数退避（`libs/agent/src/agent_loop.cpp`）

- 两个 agent-loop 变体（`run_agent_loop` / `run_agent_loop_continue`）的流式块加重试。
- **可重试**：HTTP 408/429/5xx + 传输层失败（超时/连接断/EOF）。**不重试**：400/401/403/404。
- 退避 1s→2s→4s（`1000 << attempt`），封顶 `max_retry_delay_ms`。
- 等待按 100ms 分片，abort signal 可即时打断。
- **事件缓冲**：流式事件先存 `pending`，仅在终态（成功/不可重试/重试耗尽）才 flush 到 UI → 重试不会重复打印部分文本。
- 复用 `StreamOptions::max_retries`（默认 2）/ `max_retry_delay_ms`（之前已声明但无人用）。

### 5.2 错误人话化（`libs/ai/src/stream_simple.cpp`）

`humanize_stream_error(provider, raw)` 把 `"anthropic: HTTP 429 ..."` 这类原始串映射成可操作的人话：

| 状态 | 人话 |
|---|---|
| 429 | rate limited by anthropic (429) — too many requests |
| 401/403 | authentication failed — check your API key, or run /login |
| 400 | bad request — provider rejected the input |
| 404 | model or endpoint not found |
| 5xx | provider server error — temporary, usually resolves on retry |
| 超时/连接 | request timed out / could not connect — check your network |
| 未知 | 裁剪到 ≤200 字符，绝不原样吐 JSON |

接入两处：interactive MessageEnd 红字、print 模式 stderr。

### 5.3 socket 卡死兜底

`http_client.cpp` 已有 `SO_RCVTIMEO` + poll 超时（`timeout_ms`/`connect_timeout_ms`）。超时表现为 `stop_reason="error"` 错误串，被 5.1 判定为可重试。用户侧强退由 Ctrl-C 两段式覆盖。

## 6. 验证

| 项 | 方法 | 结果 |
|---|---|---|
| 构建 | `cmake --build build` | 0 新增 warning |
| 单测 | `ctest`（含新增 `test_reliability`） | 13/13 |
| 终端契约 | `tools/pty_verify.py`（真实 pty 驱动 pi） | 8/8 |

`pty_verify.py` 断言：启动写 `\x1b[1;28r`（30 行）、resize 到 20 行写 `\x1b[1;18r`、无 `?1049h`、无 `?1000h/1003h/1006h`、退出写 `\x1b[r`、保留 `?2004h`。

### 待真机验证（环境受限，需人工）

- **MobaXterm**（DECSTBM 必测，不可假设）：滚轮翻历史、底部 2 行锁定、退出后聊天留存可复制、<5 行 fallback。
- **K3 RISC-V 真机**：构建 + 上述冒烟（项目唯一已验证平台）。
- **P1 真实触发**：构造 429/超时确认退避 + 人话错误。

## 7. 风险与决策记录

- DECSTBM 在 MobaXterm 兼容性：vim/tmux 能用 ≠ raw escape 一定行 → 必须实测。
- `ESC 7/ESC 8` 优于 `[s/[u`：保 SGR + 滚动区，跨终端一致。
- emit 与 redraw_bottom 光标竞争：同一 `term_mtx` 串行化。
- resize 一帧失效：SIGWINCH 只 poll，主循环重建。

## 8. 视觉对齐上游（P2-vis）

> 目标：复刻上游 `pi`（pi-spacemit）TUI 的布局、显示机制、文本对齐、内容、颜色、状态渲染。
> 数据源：`packages/tui/src/{utils,tui,terminal}.ts`、`packages/coding-agent/src/modes/interactive/{theme/dark.json,components/*}`。

### 8.1 架构说明（与上游的差异，刻意保留）

上游交互模式**不用 scroll region**，而是主屏 + **差分渲染**（diff `previousLines`/`newLines`，只重写变化行，footer 作为最后一个子组件落在底部，内容自然滚进 scrollback）。本项目 P0 选了 **DECSTBM 锁底**——机制不同，但用户可见效果（scrollback 滚轮 + 固定底部）一致，且满足先前明确确认的"滚轮翻历史"需求。因此**复刻聚焦视觉层**（颜色/布局/对齐/状态渲染），渲染引擎保留 DECSTBM 架构。

### 8.2 Theme：truecolor 调色板（`theme.{hpp,cpp}`）

完整移植上游 `dark.json` token，全部用 truecolor（`\x1b[38;2;r;g;b` / `48;2;r;g;b`）：

| token | 颜色 | 用途 |
|---|---|---|
| `user_message_bg` | `#343541` | 用户消息背景 |
| `tool_pending_bg` / `success_bg` / `error_bg` | `#282832`/`#283228`/`#3c2828` | 工具状态背景 |
| `thinking_text` | `#808080` gray | 推理流 |
| `tool_diff_added/removed/context` | green/red/gray | diff 行 |
| `custom_message_label` | `#9575cd` | `[compaction]` 标签 |
| `accent`/`error`/`warning`/`dim`/`text` … | 见 dark.json | 核心 UI |

`Theme::fg/bg/bold/italic/inverse` helper 镜像上游 `theme.fg(token,text)`。旧字段名（`primary`/`tool_pending`/…）保留为别名，避免破坏 input/footer 既有代码。

### 8.3 Width / 对齐（`render_util.{hpp,cpp}`）

镜像上游 `utils.ts`：
- `visible_width`：跳过 ANSI/OSC/APC 序列，CJK/宽字符按 2 列（复用 `pi_core::unicode::display_width`）。
- `truncate_to_width`：按可见宽度截断 + 省略号，保留样式。
- `pad_to_width` / `apply_bg_to_line`：先补齐到整行宽再上背景 → 背景铺满整行（对齐上游 `applyBackgroundToLine`）。

### 8.4 消息渲染器（`message_render.{hpp,cpp}`）

| 组件 | 上游布局 | 本项目 |
|---|---|---|
| user message | `Box(1,1)` + userMessageBg + userMessageText | `user_message()`：padY=1 空背景行 + padX=1 + 整行背景 |
| assistant text | padding x=1，无背景 | `assistant_text()` |
| thinking | italic + thinkingText，pad x=1 | `thinking_text()` |
| tool execution | `Spacer(1)` + `Box(1,1)` + 状态背景 + bold toolTitle | `tool_execution(state)`：Pending/Success/Error → 对应背景 |
| diff | `+`/`-`/` ` 前缀着色 | `diff_line()` |
| compaction | `Box(1,1)` customMessageBg + bold `[compaction]` label | `compaction_message()` |

接入 `interactive.cpp`：用户回显、resume 重放、流式 TextDelta/ThinkingDelta/ToolExecutionStart/End/ToolCallEnd 全部走渲染器。

### 8.5 Footer（`interactive.cpp` `Ui::footer_text_`）

镜像上游 `footer.ts` stats 行：左侧 status/spinner + `↑in ↓out`（`formatTokens` k/M 压缩），右侧 model 右对齐（空间不足截断），整行 dim。

### 8.6 视觉验证

- `tests/test_render.cpp`（9 用例）：visible_width/truncate/pad/bg、theme truecolor 值、user_message 整行背景 + 3 行盒子、tool_execution 状态选色、diff 选色、thinking italic。
- `tools/pty_verify.py`：真实 pty 提交含 CJK 的消息，断言输出含 `\x1b[48;2;52;53;65m`（userMessageBg）+ `\x1b[38;2;102;102;102m`（dim footer）。10/10 通过。

