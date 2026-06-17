# 130 · 子系统 `@earendil-works/pi-tui`

> 角色：差分渲染的 TUI 组件库。无 LLM 依赖、无业务逻辑。`pi-coding-agent` 交互模式、扩展 UI 都基于它。

## 1. 入口

源码：`packages/tui/src/index.ts`

```ts
// 主要 re-export（节选）
export { type AutocompleteItem, type AutocompleteProvider, ... } from "./autocomplete.ts";
export { Box } from "./components/box.ts";
export { CancellableLoader } from "./components/cancellable-loader.ts";
export { Editor, type EditorOptions, type EditorTheme } from "./components/editor.ts";
export { Image, type ImageOptions, type ImageTheme } from "./components/image.ts";
export { Input } from "./components/input.ts";
export { Loader, type LoaderIndicatorOptions } from "./components/loader.ts";
export { Markdown, ... } from "./components/markdown.ts";
export { SelectList, type SelectItem, ... } from "./components/select-list.ts";
export { type SettingItem, SettingsList, ... } from "./components/settings-list.ts";
export { Spacer } from "./components/spacer.ts";
export { Text } from "./components/text.ts";
export { TruncatedText } from "./components/truncated-text.ts";
export type { EditorComponent } from "./editor-component.ts";
export { fuzzyFilter, fuzzyMatch, type FuzzyMatch } from "./fuzzy.ts";
export {
  type Keybinding, type KeybindingConflict, ...,
  KeybindingsManager, getKeybindings, setKeybindings, TUI_KEYBINDINGS,
} from "./keybindings.ts";
export {
  type KeyEventType, type KeyId,
  Key, parseKey, matchesKey,
  isKeyRelease, isKeyRepeat, isKittyProtocolActive, setKittyProtocolActive, decodeKittyPrintable,
} from "./keys.ts";
export { StdinBuffer, type StdinBufferEventMap, type StdinBufferOptions } from "./stdin-buffer.ts";
export { ProcessTerminal, type Terminal } from "./terminal.ts";
export { parseOsc11BackgroundColor, type RgbColor } from "./terminal-colors.ts";
export { ... } from "./terminal-image.ts";
export { TUI } from "./tui.ts";
export { truncateToWidth, visibleWidth, wrapTextWithAnsi } from "./utils.ts";
```

## 2. 功能分组

### 2.1 渲染引擎

```ts
// packages/tui/src/tui.ts
export class TUI { ... }     // 差分渲染器：collect → diff → write

// packages/tui/src/terminal.ts
export interface Terminal {
  write(data: string): void;
  // ... 能力检测相关方法
}
export class ProcessTerminal implements Terminal { ... }
```

- 组件实现 `render(width): string[]` + 可选 `invalidate()`
- TUI 收集所有 component 输出，做**行级差分**写到 stdout
- 自动协商 Kitty 键盘协议
- 支持 OSC 9;4 进度上报

### 2.2 容器与布局

| 组件 | 用途 |
| --- | --- |
| `Box` | flex 布局（direction, padding, gap, border） |
| `Spacer` | 空白 |
| `Container`（内部） | 树容器 |

### 2.3 文本与渲染

| 组件 / 函数 | 用途 |
| --- | --- |
| `Text` | 单/多行文本 + ANSI |
| `TruncatedText` | 视宽截断 |
| `Markdown` | GFM 渲染 + 代码高亮 |
| `visibleWidth(s)` | 字符串的视宽（处理 CJK/Emoji） |
| `truncateToWidth(s, w)` | 按视宽截到宽度 |
| `wrapTextWithAnsi(s, w)` | ANSI 保留软换行 |
| `hyperlink` | OSC 8 链接 |
| `getGraphemeSegmenter` / `getWordSegmenter` | 共享 Intl.Segmenter |

### 2.4 输入

| 组件 | 用途 |
| --- | --- |
| `Editor` | 多行编辑：undo/redo、kill ring、word nav、autocomplete |
| `Input` | 单行 input |
| `StdinBuffer` | 原始 stdin → 行/批切分；识别 Kitty 协议序列 |
| `native-modifiers` | macOS Option/Shift 检测 |

### 2.5 选择 / 设置

| 组件 | 用途 |
| --- | --- |
| `SelectList` | 列表选择 + fuzzy 过滤 + 键盘导航 |
| `SettingsList` | 设置面板（基于 SelectList） |
| `AutocompleteProvider` | `ctx` autocomplete 钩子 |
| `CombinedAutocompleteProvider` | 多 provider 并联 |
| `fuzzyMatch(s, target)` / `fuzzyFilter(items, query)` | 模糊匹配 |

### 2.6 加载

| 组件 | 用途 |
| --- | --- |
| `Loader` | 动画 loader |
| `CancellableLoader` | 可被 Ctrl+C 取消 |
| `CancellableLoader` 配置 | 帧数组 + 帧间隔 |

### 2.7 图像协议

`packages/tui/src/terminal-image.ts`

| 函数 | 用途 |
| --- | --- |
| `detectCapabilities()` | 启动时探测（Kitty / iTerm2 / trueColor / OSC 11） |
| `getCapabilities()` | 读取探测结果 |
| `encodeKitty(buf, opts)` | → Kitty escape |
| `encodeITerm2(buf, opts)` | → iTerm2 escape |
| `allocateImageId()` / `deleteKittyImage(id)` / `deleteAllKittyImages()` | 图像 GC |
| `getImageDimensions(buf)` / `getJpegDimensions(buf)` / `getGifDimensions(buf)` | 元数据 |
| `calculateImageRows(w, h, cellW, cellH)` | 图像占行数 |
| `Image` 组件 | 内联图片 |

### 2.8 键盘系统

`packages/tui/src/keys.ts`

- `Key` 枚举（letter / digit / function / symbol / 方向键 / 功能键）
- `KeyEventType = "press" | "release" | "repeat"`
- `KeyId` 字符串类型（用于 keybinding 注册）
- `parseKey(input) → KeyEvent`
- `matchesKey(data, keyId)` — 检查输入是否匹配 key
- `isKeyRelease`, `isKeyRepeat`, `decodeKittyPrintable`
- 全局 `setKittyProtocolActive(active)` / `isKittyProtocolActive()`

`packages/tui/src/keybindings.ts`

- `Keybindings` 接口 — 所有支持的 action id
- `Keybinding` = `keyof Keybindings`
- `KeybindingDefinition` = `{ defaultKeys: KeyId | KeyId[]; description? }`
- `KeybindingsConfig` = key 映射
- `TUI_KEYBINDINGS` 默认 keymap
- `KeybindingsManager` 全局注册表
- `getKeybindings(name)`, `setKeybindings(name, keys)`

**KeyId 命名空间**（举例）：
```
tui.editor.cursorUp
tui.editor.cursorDown
tui.editor.cursorWordLeft
tui.editor.deleteWordBackward
tui.editor.undo
tui.editor.yankPop
tui.input.newLine
tui.input.submit
tui.input.tab
tui.input.copy
tui.select.up
tui.select.down
tui.select.confirm
tui.select.cancel
```

**重要约束**（AGENTS.md 强制）：禁止 `matchesKey(k, "ctrl+x")` 硬编码字符串，必须用 `KeybindingsManager.getDefault("xxx")`。

### 2.9 终端颜色

`packages/tui/src/terminal-colors.ts`

- `parseOsc11BackgroundColor(osc) → RgbColor | undefined` — 读背景色
- `RgbColor = { r: number; g: number; b: number }`

### 2.10 编辑器子系统

| 文件 | 用途 |
| --- | --- |
| `editor-component.ts` | `EditorComponent` 接口（扩展可注入自定义编辑器） |
| `editor.ts` | `Editor` 工厂 |
| `undo-stack.ts` | 字符级 undo/redo |
| `kill-ring.ts` | emacs 风格 kill ring |
| `word-navigation.ts` | 单词级光标跳跃 |

### 2.11 TUI 主类

```ts
export class TUI {
  constructor(opts: TUIOptions);
  add(component: Component, opts?: { ... }): Component;
  remove(component: Component): void;
  requestRedraw(): void;
  setWorkingIndicator(opts: { frames: string[]; intervalMs: number } | null): void;
  showOverlay(component: Component, opts?: OverlayOptions): OverlayHandle;
  // ... exit / 关闭 / 资源回收
}
```

## 3. 典型用法

### 3.1 启动一个最小 TUI

```ts
import { TUI, ProcessTerminal, Box, Text, Editor } from "@earendil-works/pi-tui";

const tui = new TUI({
  terminal: new ProcessTerminal(),
  // ...
});
const root = new Box({ flexDirection: "column" });
const editor = new Editor(tui, theme, keybindings);
root.add(new Text("Hello"));
root.add(editor);
tui.add(root);
tui.start();
```

### 3.2 自定义组件

```ts
import { Component, Text } from "@earendil-works/pi-tui";

class MyBox implements Component {
  invalidate() {}
  render(width: number): string[] {
    return [Text({ text: `width=${width}`, theme })];
  }
}
```

### 3.3 注册快捷键

```ts
import { KeybindingsManager, matchesKey } from "@earendil-works/pi-tui";

const key = KeybindingsManager.getDefault("tui.editor.undo");
if (matchesKey(input, key)) { /* undo */ }
```

## 4. 内部能力

| 路径 | 用途 |
| --- | --- |
| `tui/tui.ts` | 主类实现 |
| `tui/components/*` | 13 个内置组件 |
| `tui/terminal.ts` | 终端抽象 + ProcessTerminal |
| `tui/keys.ts` | 键盘协议解析 |
| `tui/keybindings.ts` | Keybinding 注册表 |
| `tui/stdin-buffer.ts` | 输入缓冲 |
| `tui/utils.ts` | `visibleWidth`, `truncateToWidth`, `wrapTextWithAnsi` |
| `tui/fuzzy.ts` | 模糊匹配 |
| `tui/undo-stack.ts` / `tui/kill-ring.ts` / `tui/word-navigation.ts` | 编辑器子系统 |

## 5. 不变量 / 契约

1. **`Component.render(width)` 是纯函数**（除标准时钟类 `Loader`）— 改状态走 `invalidate()`
2. **不直接写 stdout** — 必须 `TUI.requestRedraw()` 后由引擎写
3. **退出前 `showCursor()`** — 恢复光标
4. **Kitty 协议启用后 `deleteAllKittyImages()`** — 防止图像残留
5. **所有颜色走 theme** — 组件内不写死 ANSI
6. **`matchesKey` 不得硬编码字符串** — 走 KeybindingsManager
7. **共享 `Intl.Segmenter` 单例** — `getGraphemeSegmenter()` / `getWordSegmenter()`，不要 `new`
