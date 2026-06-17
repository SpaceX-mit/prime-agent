# 200 · `pi-coding-agent` 的依赖

> 源码 `packages/coding-agent/package.json`：**19 个直接依赖 + 1 个 optional + 1 个 WASM**。这是依赖最多的子系统。

## 1. 直接依赖（19 个）

### 1.1 内部包（3 个）

| 包 | 版本 | 用途 |
| --- | --- | --- |
| `@earendil-works/pi-agent-core` | `^0.79.4` | Agent 循环、Compaction、State |
| `@earendil-works/pi-ai` | `^0.79.4` | LLM provider、流式、OAuth |
| `@earendil-works/pi-tui` | `^0.79.4` | TUI 组件、键盘 |

### 1.2 解析 / 工具

| 包 | 版本 | 用途 | 调用点 | C/C++ 替代 |
| --- | --- | ---: | --- | --- |
| `chalk` | 5.6.2 | 终端颜色 | 启动 banner、stderr 输出 | 自实现 ANSI 转义 |
| `cross-spawn` | 7.0.6 | 跨平台 spawn（Node.js < 16.4 patch） | `utils/shell.ts` | POSIX `fork/execve` / Win32 `CreateProcess` |
| `diff` | 8.0.4 | unified diff 库（编辑对比） | `core/tools/edit-diff.ts` / `components/diff.ts` | 自实现 Myers diff ~100 行 |
| `glob` | 13.0.6 | 文件 glob 搜索 | `core/skills.ts` 等 | POSIX `fnmatch` + walk |
| `highlight.js` | 10.7.3 | 代码高亮 | `utils/syntax-highlight.ts` | **Pygments C port** / syntect / pygments-c |
| `hosted-git-info` | 9.0.3 | 解析 git URL（github:foo/bar） | `core/package-manager.ts` | 自实现 ~50 行 |
| `ignore` | 7.0.5 | `.gitignore` 兼容过滤 | `core/skills.ts`、`config.ts` | libgit2 / 自实现 |
| `jiti` | 2.7.0 | 运行时 TS loader（扩展） | `core/extensions/loader.ts` | 不需要（C/C++ 不做扩展） |
| `minimatch` | 10.2.5 | glob 模式匹配 | `core/skills.ts` | POSIX `fnmatch` + 自实现 `**` |
| `proper-lockfile` | 4.1.2 | 异步文件锁 | `core/settings-manager.ts` / `auth-storage.ts` | 自实现 flock + UUID（~50 行） |
| `semver` | 7.8.0 | 语义化版本 | `core/experimental.ts`、`package-manager.ts` | 自实现 ~100 行 |
| `typebox` | 1.1.38 | Tool schema | 全部内置 tool | 同 pi-ai |
| `undici` | 8.3.0 | Node HTTP 客户端（global dispatcher） | `core/http-dispatcher.ts` | libcurl |
| `yaml` | 2.9.0 | YAML 解析 | `utils/frontmatter.ts` | yaml-cpp |

### 1.3 间接但重要

- `mime` 4.x — 通过 chalk / `utils/mime.ts` 自动安装
- `tar` 7.x — 通过 proper-lockfile 自动安装
- `minipass` 4.x — tar 的依赖

## 2. optionalDependencies（1 个）

| 包 | 版本 | 用途 | C/C++ 替代 |
| --- | --- | --- | --- |
| `@mariozechner/clipboard` | 0.3.9 | **系统剪贴板**（文本 + 图像） | 见下 |

### 2.1 `@mariozechner/clipboard` 详解

- 仓库作者自己写的：<https://github.com/badlogic/clipboard>
- 工具链：**NAPI-RS**（Rust → `.node`）
- 三平台 prebuilt：darwin / linux / win32（包含 musl + riscv64）
- 导出 API：
  ```ts
  availableFormats(): string[]       // ["public.utf8-plain-text", "public.png", ...]
  hasText() / hasImage() / hasHtml() / hasRtf(): boolean
  getText() / getHtml() / getRtf() / getImageBase64(): Promise<string>
  getImageBinary(): Promise<Array<number>>
  setText(s) / setHtml(s) / setRtf(s) / setImageBase64(s) / setImageBinary(arr): Promise<void>
  clear(): Promise<void>
  watch(): void                        // 监听剪贴板变化
  ```

- macOS 后端：`NSPasteboard`
- Linux 后端：`xclip` / `wl-paste` / `xsel` 命令
- Windows 后端：`OpenClipboard` + `GetClipboardData`

**C/C++ 端口**：

| 平台 | 库 / API |
| --- | --- |
| macOS | AppKit `NSPasteboard` |
| Windows | Win32 Clipboard API |
| Linux X11 | 调 `xclip -selection clipboard -o` |
| Linux Wayland | 调 `wl-paste` |

~300 行 C++ 即可。

## 3. WASM（1 个）

### 3.1 `@silvia-odwyer/photon-node` (0.3.4)

- 仓库：<https://github.com/silvia-odwyer/photon>
- 实现：Rust → WASM
- `photon_rs_bg.wasm` ~200 KB
- 用法：`utils/photon.ts` 调用做图像 resize / convert

**调用点**：
- `utils/image-resize-core.ts` — 图像 resize
- `utils/clipboard-image.ts` — clipboard BMP → PNG
- `utils/exif-orientation.ts` — 读 EXIF orientation
- `utils/image-convert.ts` — 格式转换

**C/C++ 替代**：

| 库 | 备注 |
| --- | --- |
| **libvips** | 高性能、成熟、~50 MB |
| **stb_image + stb_image_resize** | 单头文件，~5 MB，足够 |
| **libpng + libjpeg + libwebp** | 全平台支持 |

> **推荐 stb_image**：单头文件、无外部依赖、足够 pi 用例（resize 2000×2000 + format convert）。

### 3.2 加载机制

`utils/photon.ts` 在 Node 启动时动态 `require("@silvia-odwyer/photon-node")`。在 Bun 编译时通过 `shx cp ... photon_rs_bg.wasm dist/` 把 .wasm 拷到可执行文件旁。

C/C++ 端口**不需要 WASM runtime**，直接链 stb_image。

## 4. 调用点（按功能）

| 功能 | 文件 | 用到的包 |
| --- | --- | --- |
| 系统剪贴板 | `utils/clipboard.ts`<br>`utils/clipboard-image.ts`<br>`utils/clipboard-native.ts` | `@mariozechner/clipboard`（native）<br>`cross-spawn`（Linux 回退）<br>`photon`（BMP→PNG） |
| 文件搜索 | `core/skills.ts`<br>`core/tools/find.ts` | `ignore`<br>`glob` |
| 工具参数 schema | `core/tools/*.ts` | `typebox` |
| Bash 执行 | `core/bash-executor.ts`<br>`utils/shell.ts` | `cross-spawn` |
| 图像处理 | `utils/image-resize.ts`<br>`utils/clipboard-image.ts` | `photon` (WASM) |
| 终端颜色 | 多处 | `chalk` |
| 语法高亮 | `utils/syntax-highlight.ts`<br>`modes/interactive/theme/theme.ts` | `highlight.js` |
| diff 显示 | `core/tools/edit-diff.ts`<br>`components/diff.ts` | `diff` |
| 文件锁 | `core/settings-manager.ts`<br>`core/auth-storage.ts` | `proper-lockfile` |
| git URL 解析 | `core/package-manager.ts` | `hosted-git-info` |
| 包版本比较 | `core/package-manager.ts`<br>`core/experimental.ts` | `semver` |
| HTTP | `core/http-dispatcher.ts` | `undici` |
| 扩展加载 | `core/extensions/loader.ts` | `jiti` |
| YAML / frontmatter | `utils/frontmatter.ts` | `yaml` |
| minimatch | `core/skills.ts` | `minimatch` |

## 5. devDependencies

| 包 | 用途 |
| --- | --- |
| `@types/cross-spawn` 6.0.6 | TS 类型 |
| `@types/diff` 7.0.2 | TS 类型 |
| `@types/hosted-git-info` 3.0.5 | TS 类型 |
| `@types/ms` 2.1.0 | TS 类型 |
| `@types/node` 24.12.4 | TS 类型 |
| `@types/proper-lockfile` 4.1.4 | TS 类型 |
| `@types/semver` 7.7.1 | TS 类型 |
| `shx` 0.4.0 | shell 跨平台包装（`shx cp` 等） |
| `typescript` 5.9.3 | 编译 |
| `vitest` 3.2.4 | 测试 |

## 6. 总结：pi-coding-agent 的 C/C++ 端口方案

| 能力 | 自实现 | 用现成库 |
| --- | --- | --- |
| 7 个内置工具 | ✅ ~2000 行 | — |
| Bash 后端 | ✅ ~300 行 | — |
| 扩展系统 | ✅ ~1500 行（可选） | — |
| Session 持久化 | ✅ ~300 行 | — |
| Compaction 触发器 | ✅ ~200 行 | — |
| HTML 导出 | ✅ ~800 行（自写模板） | — |
| 系统剪贴板 | ✅ ~300 行 | 调系统 API / xclip / wl-paste |
| 图像 resize | 可选 ~300 行 | **stb_image**（推荐） |
| glob / minimatch | ✅ ~200 行 | POSIX fnmatch |
| .gitignore | 可选 ~200 行 | libgit2 |
| YAML frontmatter | 可选 ~100 行 | **yaml-cpp** |
| diff | ✅ ~100 行（Myers） | — |
| semver | ✅ ~100 行 | — |
| 文件锁 | ✅ ~50 行（flock） | — |
| git URL 解析 | ✅ ~50 行 | — |
| HTTP 客户端 | — | **libcurl** |
| Syntax highlight | 可选 ~1500 行 | **Pygments**（C port：pygments-c, synless） |
| Markdown 渲染 | 可选 ~500 行 | **md4c**（与 pi-tui 共享） |
| 跨平台 spawn | ✅ ~200 行 | — |

最关键决策：
- **jiti / 扩展系统**：C/C++ 端口**不需要**（不做扩展市场）；只保留内部 hook
- **photon WASM**：C/C++ 端口**直接换 stb_image**（少一个 WASM runtime 依赖）
- **`@mariozechner/clipboard`**：C/C++ 端口**自实现**（跨平台 ~300 行）
- **highlight.js**：C/C++ 端口**换 pygments** 或自实现简化版（10-20 种语言）
