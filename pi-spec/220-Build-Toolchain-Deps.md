# 220 · 构建 / 开发依赖 (Build & Toolchain Dependencies)

> **不进入运行时**。只是开发、构建、测试、发布用的工具链。C/C++ 端口**完全不需要**这些。

## 1. 根 `package.json` devDependencies（9 个）

| 包 | 版本 | 用途 | 备注 |
| --- | --- | --- | --- |
| `@anthropic-ai/sandbox-runtime` | 0.0.26 | Anthropic 沙箱（开发演示用） | 替换为 Gondolin / Docker |
| `@biomejs/biome` | 2.3.5 | **linter + formatter**（替代 ESLint + Prettier） | 配置文件 `biome.json` |
| `@types/node` | 22.19.19 | Node.js TS 类型 | 仅 devDeps（workspace 共享） |
| `@typescript/native-preview` | 7.0.0-dev.20260120.1 | **tsgo**（原生 TS 编译器，Go 写的） | `npm run check` 调 `tsgo --noEmit` |
| `esbuild` | 0.28.0 | JS 极速打包（间接用到） | 实际没在 scripts 用 |
| `husky` | 9.1.7 | git hooks | `.husky/` |
| `jiti` | 2.7.0 | TS loader（间接） | 实际只在 coding-agent 间接 |
| `shx` | 0.4.0 | 跨平台 shell 包装（`shx cp`） | 复制资源时用 |
| `tsx` | 4.22.1 | TS 执行器 | 测试和生成脚本用 |
| `typescript` | 5.9.3 | **TS 编译器** | `tsc -p` |

> 实际 `devDependencies` 字段是 10 个；这里列 9 个是因为 esbuild 没在 scripts 直接用（可能在 devDeps 树里被隐式引）。

## 2. 根 `package.json` scripts

| 脚本 | 跑什么 | C/C++ 端口 |
| --- | --- | --- |
| `clean` | `npm run clean --workspaces` | `make clean` |
| `build` | tsc + 资源复制 | `cmake --build` |
| `check` | biome + pinned-deps + ts-imports + shrinkwrap + tsgo + browser-smoke | `clang-tidy` + 自定义检查 |
| `check:pinned-deps` | 检查 `package.json` 直连 dep pin 精确版本 | 手动 review |
| `check:ts-imports` | 检查跨包相对导入合规 | 不需要 |
| `check:shrinkwrap` | consumer shrinkwrap 一致性 | 不需要 |
| `test` | workspaces 内 `npm test` | ctest / 自实现 runner |
| `version:patch` / `minor` / `major` | `npm version` + 同步 | bump.sh |
| `prepublishOnly` | clean + build + check | `make` |
| `release:local` | `node scripts/local-release.mjs` | 自实现 |
| `release:patch` / `minor` | `node scripts/release.mjs` | 自实现 |
| `shrinkwrap:coding-agent` | 生成 consumer shrinkwrap | 不需要 |

## 3. 工作流脚本（`scripts/`）

| 脚本 | 用途 |
| --- | --- |
| `scripts/build-npm.sh` | 一键 build + pack + 烟测（K3 用） |
| `scripts/local-release.mjs` | 4 包 clean → build → `npm pack` 集中到 `--out` |
| `scripts/release.mjs` | 完整发版（CHANGELOG + commit + tag + push） |
| `scripts/release-notes.mjs` | GitHub release 备注 |
| `scripts/publish.mjs` | npm publish（CI 用 OIDC 替代） |
| `scripts/generate-coding-agent-shrinkwrap.mjs` | 生成 consumer shrinkwrap |
| `scripts/check-pinned-deps.mjs` | dep 精度检查 |
| `scripts/check-ts-relative-imports.mjs` | TS 相对导入扫描 |
| `scripts/check-browser-smoke.mjs` | 浏览器烟测 |
| `scripts/sync-versions.js` | 4 包版本同步 |
| `scripts/profile-coding-agent-node.mjs` | tui / rpc 性能 profile |
| `scripts/generate-models.ts` | `MODELS` 重生成器 |
| `scripts/generate-image-models.ts` | `IMAGES_MODELS` 重生成器 |

> C/C++ 端口**不需要以上任何脚本**。对应功能：
> - 同步版本：`Makefile` 内一个变量
> - 生成模型表：C++ 用 `std::vector<Model>` 静态初始化
> - 发版：`scripts/release-cpp.sh` 自写

## 4. `.husky/` Git Hooks

仓库有 pre-commit hook：检查 `package-lock.json` / npm-shrinkwrap.json 改动。设 `PI_ALLOW_LOCKFILE_CHANGE=1` 才能绕过。

C/C++ 端口**不需要**；git hook 可改为运行 `ctest` / `clang-format`。

## 5. `.npmrc` 配置

```
save-exact=true
min-release-age=2
```

C/C++ 端口无 npm，纯 C++ 工具链：
- `conan` / `vcpkg`（C++ 包管理）→ 用 pin
- `cmake` / `meson` → 配置构建
- `clang-format` / `clang-tidy` → 风格

## 6. 间接 npm 依赖（lockfile 里出现但运行时不需要的）

通过 5 大 Provider SDK 引入了大量间接依赖。C/C++ 端口**完全跳过**：

- `@aws-crypto/*` (4-5 个)
- `@aws-sdk/util-*`, `@aws-sdk/middleware-*` (50+ 个)
- `@smithy/*` (20+ 个)
- `@types/*` (15+ 个，仅 devDeps)
- `mime`, `minipass`, `tar`, `chownr`, `fs-minipass` (通过 chalk / proper-lockfile)
- `p-retry`, `headers-polyfill` (通过 anthropic-sdk)

**lockfile 总计**：约 200 个 npm 包（仅作 transitive deps）。

## 7. 编译产物

| 文件 | 来源 | 大小 |
| --- | --- | ---: |
| `packages/ai/dist/*.js` | tsc 编译 | ~300 KB |
| `packages/agent/dist/*.js` | tsc | ~100 KB |
| `packages/tui/dist/*.js` | tsc | ~150 KB |
| `packages/coding-agent/dist/*.js` | tsc | ~3 MB（含 HTML 模板 + 主题） |
| `packages/coding-agent/dist/photon_rs_bg.wasm` | 复制自 photon-node | ~200 KB |
| `packages/tui/native/*/prebuilds/*/*.node` | .c 编译（N-API） | ~50 KB each |
| `node_modules/@mariozechner/clipboard*/*.node` | Rust 编译（NAPI-RS） | ~500 KB each |

C/C++ 端口**单二进制**（如 `pi` 静态链接）：
- 全部代码 ~50,000 行 C/C++ → 编译后 ~5 MB
- libcurl / OpenSSL → 动态链接（系统包）
- stb_image → header-only
- libgit2 / yaml-cpp / md4c / pygments → 动态链接（系统包）

## 8. 工具链替代方案（C/C++ 端口）

| 当前 Node 工具 | C/C++ 替代 |
| --- | --- |
| `tsc` | `clang++` / `g++` |
| `tsgo`（native 编译） | 不需要 |
| `vitest` | `ctest` + `doctest` / `Catch2` |
| `biome` (lint+format) | `clang-format` + `clang-tidy` |
| `npm` (包管理) | `conan` / `vcpkg` / `xmake` |
| `npm pack` (打 tarball) | `cpack` |
| `husky` (git hook) | 自己写 |
| `tsx` / `node` (执行 TS) | 直接跑 `pi` |

## 9. 不要做

| 错误做法 | 原因 |
| --- | --- |
| 用 AWS SDK C++ | 50+ MB；只调一个 `ConverseStreamCommand` |
| 用 libwebsockets 实现 Codex | Codex 协议特殊；可考虑先跳过 |
| 用 libmicrohttpd 实现 OAuth callback | 太重；~100 行 C 即可 |
| 用 Electron | 跨平台 GUI 框架；pi 是纯 TUI |
| 引入 Qt / ncurses | 自己实现的差分渲染更紧凑 |
