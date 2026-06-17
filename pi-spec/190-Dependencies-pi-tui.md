# 190 · `pi-tui` 的依赖

> 源码 `packages/tui/package.json`：2 个直接依赖 + 2 个 native module（含 .c 源码）。

## 1. 直接依赖（2 个）

| 包 | 版本 | 用途 | 调用点 | C/C++ 替代 |
| --- | --- | ---: | --- | --- |
| `get-east-asian-width` | 1.6.0 | CJK / Emoji 字符宽度判断 | `utils.ts` 中 `visibleWidth` / `truncateToWidth` / `wrapTextWithAnsi` | **自实现**（< 50 行，依据 Unicode East Asian Width） |
| `marked` | 15.0.12 | Markdown → HTML/Tokens 解析 | `components/markdown.ts` | **cmark**（C 库）/ **md4c** / 自实现简化版 |

> `marked` 的 `Marked` 类提供 tokenizer / renderer 接口；C/C++ 端口可换 `md4c`（纯 C，~3000 行）+ ANSI 渲染器。

## 2. devDependencies

| 包 | 用途 |
| --- | --- |
| `@xterm/headless` 5.5.0 | 终端模拟（测试用） |
| `chalk` 5.6.2 | 终端颜色（开发） |

## 3. Native Modules（2 个）

仓库自带 .c 源码，**编译产物是 `.node` 文件**（N-API 兼容）。

### 3.1 `darwin-modifiers.node`（macOS 专用）

源码：`packages/tui/native/darwin/src/darwin-modifiers.c`

| 项 | 值 |
| --- | --- |
| 平台 | `darwin` (x64 + arm64) |
| 编译产物 | `packages/tui/native/darwin/prebuilds/darwin-{arch}/darwin-modifiers.node` |
| 系统框架 | `CoreGraphics.framework` (CGEventSourceFlagsState) |
| Node API | 通过 `dlsym(RTLD_DEFAULT, "napi_*")` 动态加载 napi symbols |
| 导出函数 | `isModifierPressed(name: "shift" \| "command" \| "control" \| "option"): boolean` |

**C 调用面**（C/C++ 端口直接调用，无需 Node）：

```c
#include <CoreGraphics/CoreGraphics.h>

CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
bool shift_pressed  = (flags & kCGEventFlagMaskShift)   != 0;
bool cmd_pressed    = (flags & kCGEventFlagMaskCommand) != 0;
bool ctrl_pressed   = (flags & kCGEventFlagMaskControl) != 0;
bool alt_pressed    = (flags & kCGEventFlagMaskAlternate)!= 0;
```

**C/C++ 端口**：~30 行 C，直接链 `CoreGraphics.framework`，无需 N-API。

### 3.2 `win32-console-mode.node`（Windows 专用）

源码：`packages/tui/native/win32/src/win32-console-mode.c`

| 项 | 值 |
| --- | --- |
| 平台 | `win32` (x64 + arm64) |
| 编译产物 | `packages/tui/native/win32/prebuilds/win32-{arch}/win32-console-mode.node` |
| 系统 API | `SetConsoleMode`, `GetConsoleMode`, `ENABLE_VIRTUAL_TERMINAL_INPUT` |
| 导出函数 | `enableVirtualTerminalInput(): boolean` |

**C 调用面**（C/C++ 端口直接调用）：

```c
#include <windows.h>

HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
DWORD mode;
if (GetConsoleMode(h, &mode)) {
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_INPUT);
}
```

**C/C++ 端口**：~10 行 C，直接调 Win32 API，无需 N-API。

### 3.3 加载机制

```ts
// packages/tui/src/native-modifiers.ts
const nativePath = path.join("native", "darwin", "prebuilds", `darwin-${arch}`, "darwin-modifiers.node");
const helper = cjsRequire(modulePath) as { isModifierPressed(name): boolean };
```

`pi-tui` 的 .c 文件都用 `dlsym(RTLD_DEFAULT, "napi_*")` 动态绑定 NAPI symbols，**避免硬依赖 node-api-headers**。这种"无 NAPI 头文件"写法使同一份 .c 可被多种 Node 版本加载。

**C/C++ 端口的简化**：不需要 N-API，直接 `extern "C"` 暴露函数，链接到 C++ 主程序即可。

## 4. 终端能力探测（自实现，不引依赖）

`terminal.ts` 启动时通过写特定序列探测：

```c
\x1b[>7u   // Kitty 键盘协议 query (push flags 7)
\x1b[?u    // Kitty 键盘协议 push
\x1b[c     // Primary Device Attributes (DA1)
```

读响应后解析：

```c
// CSI ? <flags> u   → Kitty 标志位
// CSI ? ... c        → 终端识别（VT100 / VT220 / xterm / iTerm2 / kitty / alacritty...）
```

完整探测在 `terminal.ts:detectCapabilities()`，C/C++ 端口**自实现**~150 行。

## 5. 协议实现

| 协议 | 复杂度 | C/C++ 端口 |
| --- | --- | --- |
| Kitty 键盘协议（push flags 7） | 中 | ~200 行 |
| Kitty image（`a=f / a=t / d=i / d=q`） | 中 | ~200 行 |
| iTerm2 image（OSC 1337） | 低 | ~80 行 |
| OSC 11 背景色探测 | 低 | ~20 行 |
| OSC 9;4 进度 | 低 | ~20 行 |
| OSC 8 hyperlink | 低 | ~20 行 |
| ANSI escape 解析 | 中 | ~300 行 |
| 行级差分 | 中 | ~200 行 |

## 6. 总结：pi-tui 的 C/C++ 端口方案

| 能力 | 自实现 | 用现成库 |
| --- | --- | --- |
| 差分渲染引擎 | ✅ ~800 行 | — |
| 组件（11 个） | ✅ ~2000 行 | — |
| 键盘系统 + Kitty 协议 | ✅ ~600 行 | — |
| 图像协议（Kitty / iTerm2） | ✅ ~400 行 | — |
| ANSI 解析 | ✅ ~400 行 | — |
| 终端能力探测 | ✅ ~150 行 | — |
| 文本宽度（CJK / Emoji） | ✅ ~50 行 | 可选 `wcwidth` |
| Markdown 渲染 | 可选 ~500 行 | **md4c**（推荐） |
| Native 修饰键 (macOS) | ✅ ~30 行 C | — |
| Native console mode (Win) | ✅ ~10 行 C | — |

`pi-tui` 是**对 C/C++ 端口最友好的子系统**——几乎没有 npm 依赖，2 个 native .c 文件是 ~50 行内可直接复用的。
