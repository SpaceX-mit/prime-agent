# 02 · 构建与发布 (Build and Release)

> 怎么把这个 monorepo 编译出来，怎么发版，依赖治理怎么管。

## 1. 构建拓扑

```
pi-tui  →  pi-ai  →  pi-agent-core  →  pi-coding-agent
                                    
            (依 package.json 的 workspaces 声明)
```

`packages/*/package.json` 内置 `build` 脚本（均为 `tsc -p` + 资源复制）。根 `package.json` 的 `build` 串行调用四包：

```jsonc
"build": "cd packages/tui && npm run build && cd ../ai && npm run build && cd ../agent && npm run build && cd ../coding-agent && npm run build"
```

## 2. 一键本地构建（K3 RISC-V 场景）

`scripts/build-npm.sh` 是 `npm run release:local` 的薄包装，提供产物校验 + 可选烟测。

```bash
# 最常用
./scripts/build-npm.sh

# 严格 check + 不打 wrapper tarball
./scripts/build-npm.sh --with-check --no-tar

# 带烟测（装到临时目录跑 --version / --list-models）
./scripts/build-npm.sh --smoke-test
```

参数：

| 选项 | 含义 |
| --- | --- |
| `--out <dir>` | 产物目录（默认 `/tmp/pi-local-release`，必须在仓库外） |
| `--with-check` | 跑 `npm run check` |
| `--smoke-test` | 装到临时目录跑 `--version` + `--list-models` 验证 |
| `--no-tar` | 跳过外层 `pi-<ver>-riscv64.tar.gz` 打包 |
| `--install-deps` | `node_modules` 缺失时自动 `npm install --ignore-scripts` |

## 3. 产物布局

```
/tmp/pi-local-release/
└── tarballs/
    ├── earendil-works-pi-ai-0.79.4.tgz             (~414K)
    ├── earendil-works-pi-tui-0.79.4.tgz            (~278K)
    ├── earendil-works-pi-agent-core-0.79.4.tgz     (~172K)
    └── earendil-works-pi-coding-agent-0.79.4.tgz   (~4.4M，含 docs/ + examples/)
```

四个 tarball **必须同一条 `npm install` 命令装**。`pi-coding-agent` 依赖另外 3 个本地包，单装会因 registry 找不到而失败。

## 4. CI 检查（`npm run check`）

依次跑：

1. `biome check --write --error-on-warnings .` — 格式化 + lint
2. `npm run check:pinned-deps` — 直连 dep 必须是精确版本
3. `npm run check:ts-imports` — TS 跨包相对导入扫描
4. `npm run check:shrinkwrap` — `packages/coding-agent/npm-shrinkwrap.json` 与根 lockfile 一致
5. `tsgo --noEmit` — 类型检查（`@typescript/native-preview`）
6. `npm run check:browser-smoke` — 浏览器烟测（`scripts/check-browser-smoke.mjs`）

> 任何一步失败都不允许提交。

## 5. 发版流程

```bash
# 1. 确保 [Unreleased] 已审计
# 2. 本地烟测
npm run release:local -- --out /tmp/pi-local-release --force

# 3. 升版本并发布
PI_ALLOW_LOCKFILE_CHANGE=1 \
  npm_config_min_release_age=0 \
  npm run release:patch    # 或 release:minor
```

`release.mjs patch|minor` 会：

1. 调用 `version:patch` / `version:minor`（`npm version -ws --no-git-tag-version` + `scripts/sync-versions.js` + `npm install --package-lock-only --ignore-scripts`）
2. 更新 4 个 `packages/*/CHANGELOG.md` 的 `[Unreleased]` 段
3. 跑 `npm run check`
4. 提交 `Release vX.Y.Z`
5. 打 tag `vX.Y.Z`
6. 提交 `Add [Unreleased] section for next cycle`
7. `git push` main + tag

CI 工作流 `.github/workflows/build-binaries.yml` 在 tag 推送时触发，`publish-npm` job 用 GitHub OIDC 信任发布到 npm。

> **lockstep 版本**：四个包**永远共享一个版本号**。改一个要改全部。

## 6. 依赖治理（核心规则）

| 规则 | 机制 |
| --- | --- |
| 直连 dep pin 精确版本 | `.npmrc` 设 `save-exact=true` |
| 避免同日 release | `.npmrc` 设 `min-release-age=2`（发版时用 `npm_config_min_release_age=0` 临时覆盖） |
| 唯一 lockfile | 根 `package-lock.json` |
| 提交拦截 | pre-commit 检查 lockfile 改动；设 `PI_ALLOW_LOCKFILE_CHANGE=1` 才能绕过 |
| 消费者侧固定 | `packages/coding-agent/npm-shrinkwrap.json` 由 `scripts/generate-coding-agent-shrinkwrap.mjs` 从根 lockfile 生成；带显式 allowlist 控制 lifecycle script deps |
| Lifecycle script 审查 | 任何新加的带 lifecycle 的 dep 必须显式 allowlist；`shrinkwrap:coding-agent` 失败会卡住 `npm run check` |
| 烟测 | `npm run release:local` 必须用 `--ignore-scripts`；CI 用 `npm ci --ignore-scripts`；定时 workflow 跑 `npm audit --omit=dev` + `npm audit signatures --omit=dev` |

## 7. K3 RISC-V 部署精简流程

```bash
# 构建机
./scripts/build-npm.sh
scp /tmp/pi-local-release/tarballs.tar.gz user@k3:~/

# K3 上
mkdir -p ~/pi-runtime && cd ~/pi-runtime
tar xzf ~/tarballs.tar.gz
npm init -y >/dev/null
npm install --omit=dev ./pi-local-release/tarballs/*.tgz
export PATH="$HOME/pi-runtime/node_modules/.bin:$PATH"
pi --version    # 0.79.4
```

K3 唯一外部要求：Node ≥ 22.19（K3 自带 22.22.x 满足）。不需要 bun、gcc、python、make。

## 8. 脚本目录速查

| 脚本 | 用途 |
| --- | --- |
| `scripts/build-npm.sh` | 一键 build+pack+可选烟测（推荐） |
| `scripts/local-release.mjs` | 4 包 clean → build → `npm pack` 集中到 `--out` |
| `scripts/release.mjs` | patch/minor 完整发版（CHANGELOG + commit + tag + push） |
| `scripts/release-notes.mjs` | GitHub release 备注修复 (`fix-github-releases`) |
| `scripts/publish.mjs` | 真正的 npm publish（CI 用 OIDC 替代） |
| `scripts/generate-coding-agent-shrinkwrap.mjs` | 生成 consumer 侧 shrinkwrap，带 `--check` |
| `scripts/check-pinned-deps.mjs` | 直连 dep 版本精度检查 |
| `scripts/check-ts-relative-imports.mjs` | TS 相对导入合规 |
| `scripts/check-browser-smoke.mjs` | 浏览器烟测 |
| `scripts/sync-versions.js` | 把一个包的 version 同步到其余三个 |
| `scripts/profile-coding-agent-node.mjs` | tui/rpc 性能 profile |
| `scripts/generate-models.ts` | 重新生成 `packages/ai/src/models.generated.ts` |
