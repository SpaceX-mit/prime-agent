# 180 · `pi-agent-core` 的依赖

> 源码 `packages/agent/package.json`：3 个直接依赖。

## 1. 直接依赖（3 个）

| 包 | 版本 | 用途 | 调用点 | C/C++ 替代 |
| --- | --- | ---: | --- | --- |
| `@earendil-works/pi-ai` | `^0.79.4` | 内部包：流式、类型、provider | 全部 `harness/*` 与 `types.ts` | 走 pi-ai 端口方案（见 [170](./170-Dependencies-pi-ai.md)） |
| `ignore` | 7.0.5 | `.gitignore` 解析（与 git 一致） | `harness/skills.ts` + `harness/env/nodejs.ts` | **gitignore 解析**（C++ 库：`git2` 的 `gitignore_parse`，或自实现） |
| `typebox` | 1.1.38 | Tool 参数 schema | `harness/types.ts` + `agent-loop.ts` | nlohmann/json + 自实现 schema |
| `yaml` | 2.9.0 | YAML frontmatter 解析（skill / prompt metadata） | `harness/skills.ts` + `harness/prompt-templates.ts` | **yaml-cpp** |

> `^0.79.4` 是 npm 范围符号；monorepo 内实际是 pin 精确版本。

## 2. devDependencies

| 包 | 用途 |
| --- | --- |
| `@types/node` 24.12.4 | TS 类型 |
| `@vitest/coverage-v8` 3.2.4 | 覆盖率 |
| `typescript` 5.9.3 | 编译 |
| `vitest` 3.2.4 | 测试 |

## 3. 内部依赖详解

### 3.1 `ignore` (7.0.5)

`harness/skills.ts` 完整引用了 `ignore` 包做 gitignore 兼容。能力：
- 加载多个 `.gitignore`/`.ignore`/`.fdignore` 文件
- 多级目录叠加
- `!pattern` 否定
- `\` 转义
- 性能：单文件 ~ms 级

**C/C++ 替代**：

| 库 | 备注 |
| --- | --- |
| `libgit2` 的 `git_ignore_*` 函数 | 与 git 行为**完全一致**；推荐 |
| 自实现 | 简单子集 ~200 行；不需完整 git 兼容 |

`harness/env/nodejs.ts` 也用 `ignore` 做 cwd 内 .gitignore 过滤。

### 3.2 `typebox` (1.1.38)

**用法**：
- 工具参数 schema：`Type.Object({ name: Type.String() })`
- TS 类型推导：`type Params = Static<typeof schema>`
- 校验：`Value.Check(schema, args)` / `Value.Errors(schema, args)`

`agent-core` 内的 schema 用法都集中在 `harness/types.ts` + `agent.ts` + `agent-loop.ts`。

**C/C++ 替代**：
- 不需要完整 schema 系统；可只取 `name` + JSON Pointer / 简单 key-value
- 工具参数用 JSON + 简单手写校验
- 复杂情况可引 `valijson` 或 `JSON-Schema-Validator`

### 3.3 `yaml` (2.9.0)

**用法**：
- `yaml.parse(text)` — 解析 YAML
- 只用于 frontmatter：`---\nkey: value\n---\nbody`

**C/C++ 替代**：
- **yaml-cpp**（成熟、广泛使用）
- **libyaml**（C 库）
- 不需要完整 YAML 1.2；只支持 scalar / mapping / sequence 即可

## 4. 间接依赖

只通过 `@earendil-works/pi-ai` 带入（见 [170 §5](./170-Dependencies-pi-ai.md)）。
`ignore` / `typebox` / `yaml` 都是零依赖。

## 5. 总结：pi-agent-core 的 C/C++ 端口方案

| 能力 | 自实现 | 用现成库 |
| --- | --- | --- |
| Agent 循环（`agentLoop`） | ✅ ~400 行 | — |
| Compaction | ✅ ~300 行 | — |
| 状态机 + 钩子 | ✅ ~300 行 | — |
| Session 抽象 | ✅ ~200 行 | — |
| UUIDv7 | ✅ ~30 行 | — |
| Skills 加载 | ✅ ~200 行 | — |
| `.gitignore` 解析 | 可选 ~300 行 | **libgit2**（推荐） |
| YAML frontmatter | 可选 ~200 行 | **yaml-cpp**（推荐） |
| Tool schema 校验 | 简化版 ~150 行 | 可选 valijson |
| Proxy | — | libcurl |

`pi-agent-core` 在 C/C++ 端口中**没有 native 依赖**；所有逻辑都是数据处理。
