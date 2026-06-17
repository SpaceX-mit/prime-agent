# 30 · 模块 `@earendil-works/pi-tui`

> 角色：差分渲染的终端 UI 组件库。无业务逻辑，无 LLM 依赖。`pi-coding-agent` 的交互模式、扩展 UI 都基于它。

## 1. 入口与导出面

源码：`packages/tui/src/index.ts`

| 类别 | 关键符号 |
| --- | --- |
| 渲染引擎 | `TUI`（差分渲染器）、`ProcessTerminal`（进程内终端实现） |
| 容器 | `Box`（flex 布局）、`Spacer` |
| 文本 | `Text`、`TruncatedText`、`Markdown`、`visibleWidth`、`truncateToWidth` |
| 输入 | `Input`、`Editor`（多行编辑）、`EditorComponent` 接口 |
| 选择 | `SelectList`、`SettingsList`、`CombinedAutocompleteProvider` |
| 加载 | `Loader`、`CancellableLoader` |
| 图片 | `Image`、`terminal-image` 模块（Kitty / iTerm2 协议） |
| 键位 | `Key`, `parseKey`, `matchesKey`, `KeybindingsManager`, `TUI_KEYBINDINGS`, `setKeybindings` |
| 平台 | `ProcessTerminal`, `setKittyProtocolActive`, `isKittyProtocolActive` |
| 终端能力 | `getCapabilities`, `detectCapabilities`, `terminal-colors.ts` (`parseOsc11BackgroundColor`) |
| 模糊匹配 | `fuzzyMatch`, `fuzzyFilter` |
| 撤销 | `undo-stack.ts`, `kill-ring.ts` |
| 标准输入 | `StdinBuffer`（批量切分） |
| 工具 | `hyperlink`, `word-navigation.ts`, `utils.ts` |

## 2. 渲染模型

```
+--------------------------------------------------+
|                       TUI                        |   ← 差分渲染
|  ┌──────────┐  ┌──────────┐  ┌──────────┐        |
|  │ Component│  │ Component│  │ Component│ ...    |
|  └──────────┘  └──────────┘  └──────────┘        |
|         ▲             ▲             ▲            |
|         │  layout (flex / box)     │            |
|         └─────────────┼─────────────┘            |
|                       │                          |
|                 writeToTerminal                 |   ← 走 ANSI escape
+--------------------------------------------------+
```

- 每个 `Component` 必须实现 `render(width): string[]` 与可选 `invalidate()`
- TUI 收集所有 component 输出，做**行级差分**写到 stdout
- 支持 Kitty 键盘协议自动协商（`parseKeyboardProtocolNegotiationSequence`）
- 支持 OSC 9;4 进度上报（`TERMINAL_PROGRESS_*_SEQUENCE`）

## 3. 核心组件

| 组件 | 用途 |
| --- | --- |
| `Box` | flex 容器，可设置 direction、padding、gap |
| `Text` | 单行/多行文本，支持 ANSI 颜色 |
| `TruncatedText` | 视宽截断 |
| `Markdown` | GFM 渲染、代码高亮 |
| `Editor` | 多行编辑器：undo/redo、kill ring、word navigation、autocomplete |
| `Input` | 单行 input |
| `SelectList` | 列表选择（fuzzy 过滤、键盘导航） |
| `SettingsList` | 设置面板（基于 `SelectList`） |
| `Loader` | 动画 loader |
| `CancellableLoader` | 可被 Ctrl+C 取消的 loader |
| `Image` | 内联图片（Kitty 协议或 iTerm2） |
| `Spacer` | 空白 |

## 4. 键盘系统

`keys.ts`：

- `Key` 枚举 + `parseKey(input) → KeyEvent`
- `KeyEventType` = `press` / `release` / `repeat`
- `KeyId` 类型（用于 `KeybindingsManager` 注册）
- `isKeyRelease`, `isKeyRepeat`, `decodeKittyPrintable`
- Kitty 协议自动协商，flag 7 = `report_events` + `report_alternate_keys` + `report_all_keys_as_escape_codes`

`keybindings.ts`：

- `KeybindingsManager` 全局 key → action 映射
- `getKeybindings`, `setKeybindings`, `matchesKey` 暴露给扩展
- `TUI_KEYBINDINGS` 默认 keymap
- `Keybinding`, `KeybindingDefinition`, `KeybindingDefinitions`, `KeybindingConflict`

> **重要约束**（AGENTS.md 强制）：禁止写 `matchesKey(k, "ctrl+x")` 这类硬编码字符串。必须把默认 key 加到 `DEFAULT_EDITOR_KEYBINDINGS` 或 `DEFAULT_APP_KEYBINDINGS`，再 `matchesKey(k, KeybindingsManager.getDefault("xxx"))`。

## 5. 终端能力

`terminal.ts` + `terminal-colors.ts` + `terminal-image.ts`：

- 抽象 `Terminal` interface；`ProcessTerminal` 是默认实现
- `getCapabilities()` → `{ kittyKeyboard, iterm2Images, trueColor, oscHyperlinks, ... }`
- `detectCapabilities()` 启动时探测
- `parseOsc11BackgroundColor` 读背景色用于主题适配
- 图像协议：
  - **Kitty**：`encodeKitty(buf, opts)` → 转义序列
  - **iTerm2**：`encodeITerm2(buf, opts)` → 转义序列
  - `allocateImageId`, `deleteKittyImage`, `deleteAllKittyImages` 用于 GC

## 6. 输入流

`stdin-buffer.ts` 暴露 `StdinBuffer`：

- 把原始 stdin 字节按行/批次切分
- 内部识别 Kitty 协议协商序列
- 输出事件：`data` / `escape` / `paste` 等

`native-modifiers.ts`：macOS 上检测 Option/Shift 修饰键是否真的按下（用于分清 alt+enter vs enter）。

## 7. 编辑器子系统

`editor-component.ts`：

- `EditorComponent` 接口（扩展可注入自定义编辑器）
- `Editor` 工厂：`new Editor(tui, theme, keybindings)`
- `undo-stack.ts`：每字符级 undo/redo
- `kill-ring.ts`：emacs 风格 kill ring
- `word-navigation.ts`：Ctrl+Left/Right 的单词跳跃

## 8. Autocomplete

`autocomplete.ts`：

- `AutocompleteProvider` 接口
- `CombinedAutocompleteProvider`（多 provider 并联）
- `SlashCommand`, `AutocompleteItem`, `AutocompleteSuggestions`
- `fuzzyMatch`, `fuzzyFilter` 用于 `@file` / `/command` 搜索

## 9. 主题

无独立主题模块，主题色由 `pi-coding-agent` 提供（`modes/interactive/theme/`）。`pi-tui` 仅暴露 `EditorTheme` / `MarkdownTheme` / `SelectListTheme` / `SettingsListTheme` / `ImageTheme` / `DefaultTextStyle` 类型。

## 10. 不变量

1. **绝不阻塞**主线程：所有 IO 走 async
2. `Component.render(width)` 必须是**纯函数**（除标准时钟类 Loader）；改 UI 状态走 `invalidate()`
3. 写终端前必须 `TUI.requestRedraw()`，不能直接 `process.stdout.write`
4. 退出前必须 `terminal.showCursor()` 恢复光标
5. Kitty 协议启用后必须 `deleteAllKittyImages()` 防止图像残留
6. 任何颜色构造都走 theme，不允许在组件内部硬编码 ANSI

## 11. 测试

`packages/tui/test/`：

- `truncate-to-width.test.ts`
- `ansi-utils.test.ts`
- `fuzzy.test.ts`（在 packages/tui）
- 视觉测试用 `interactive-mode-*` 在 `pi-coding-agent/test/`
