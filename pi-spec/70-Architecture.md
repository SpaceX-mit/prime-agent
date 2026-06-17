# 70 · 模块架构图 (Module Architecture)

> 模块级别的分层与依赖关系。所有图均用 Mermaid 描述，可在支持 Mermaid 的 Markdown 渲染器（VSCode、GitHub、Obsidian 等）直接查看。

## 1. 全局包依赖图（最粗粒度）

```mermaid
graph LR
    USER([用户 / IDE / CI])

    subgraph MONO[pi-mono monorepo]
        direction TB

        subgraph CA[packages/coding-agent]
            CLI[cli.ts / main.ts]
            CORE[core/<br/>AgentSession · SessionManager · Tools · Extensions]
            MODES[modes/<br/>interactive · print · rpc]
        end

        subgraph AG[packages/agent]
            LOOP[agentLoop<br/>agent.ts]
            HARNESS[harness/<br/>compaction · session · skills]
        end

        subgraph AI[packages/ai]
            REG[registerApiProvider<br/>registerOAuthProvider]
            PROV[providers/<br/>anthropic · openai · google · ...]
            STR[streamSimple<br/>completeSimple]
        end

        subgraph TUI[packages/tui]
            ENGINE[TUI 差分引擎]
            COMP[components<br/>Box · Editor · SelectList · Markdown]
        end
    end

    NPM[(npm registry<br/>间接依赖)]

    USER -->|pi| CLI
    CLI --> CORE
    CLI --> MODES
    MODES --> CORE
    MODES --> TUI
    CORE --> AG
    CORE --> AI
    CORE --> TUI
    AG --> AI
    AG -.->|imports from| TUI
    AI --> NPM
    TUI --> NPM
    AG --> NPM
    CA --> NPM
```

**不变方向**：`pi-coding-agent` 可以依赖其它三包；`pi-agent-core` 只能依赖 `pi-ai`；`pi-tui` 独立；`pi-ai` 独立。**无环**。

## 2. `pi-coding-agent` 内部分层

```mermaid
graph TB
    subgraph L0[CLI 边界]
        A1[cli/args.ts<br/>parseArgs]
        A2[cli/list-models.ts]
        A3[cli/file-processor.ts]
        A4[cli/initial-message.ts]
        A5[cli/startup-ui.ts<br/>first-time setup]
        A6[cli/session-picker.ts]
    end

    subgraph L1[运行时]
        B1[main.ts<br/>bootstrap]
        B2[core/agent-session-runtime.ts]
        B3[core/agent-session-services.ts]
        B4[core/sdk.ts<br/>createAgentSession]
    end

    subgraph L2[核心引擎]
        C1[core/agent-session.ts<br/>AgentSession class]
        C2[core/session-manager.ts]
        C3[core/compaction/]
        C4[core/tools/<br/>bash · read · write · edit · grep · find · ls]
        C5[core/extensions/<br/>ExtensionRunner]
        C6[core/model-registry.ts]
        C7[core/auth-storage.ts]
        C8[core/settings-manager.ts]
        C9[core/resource-loader.ts]
        C10[core/system-prompt.ts]
        C11[core/bash-executor.ts]
        C12[core/http-dispatcher.ts]
    end

    subgraph L3[三种模式]
        D1[modes/interactive/<br/>interactive-mode.ts]
        D2[modes/print-mode.ts]
        D3[modes/rpc/rpc-mode.ts]
    end

    A1 --> B1
    A2 --> B1
    A3 --> B1
    A4 --> B1
    A5 --> B1
    A6 --> B1
    B1 --> B2
    B1 --> B3
    B1 --> B4
    B2 --> C1
    B3 --> C1
    B4 --> C1
    C1 --> C2
    C1 --> C3
    C1 --> C4
    C1 --> C5
    C1 --> C6
    C1 --> C7
    C1 --> C8
    C1 --> C9
    C1 --> C10
    C4 --> C11
    C6 --> C7
    C1 --> C12
    C1 --> D1
    C1 --> D2
    C1 --> D3
```

`AgentSession`（L2）是枢纽：被三个模式共享；被 CLI / SDK 间接持有；L2 内所有子系统只对 `AgentSession` 暴露 `addEventListener` / 方法调用。

## 3. 扩展系统架构

```mermaid
graph TB
    subgraph EXT_DIR[~/.pi/agent/extensions/*.ts<br/>.pi/extensions/*.ts]
        E1[extension A]
        E2[extension B]
        E3[extension C]
    end

    subgraph CORE[core/extensions/]
        LOADER[loader.ts<br/>loadExtensions]
        RUNNER[runner.ts<br/>ExtensionRunner]
        WRAPPER[wrapper.ts<br/>wrapRegisteredTools]
    end

    subgraph CTX[运行时上下文]
        UIC[ExtensionUIContext<br/>notify · confirm · input · select · custom]
        CC[ExtensionCommandContext]
        ACTS[ExtensionContextActions<br/>sendMessage · setModel · ...]
    end

    subgraph AGENT[Agent / AgentSession]
        AHK[beforeToolCall<br/>afterToolCall 钩子]
        EVT[AgentEvent 流]
    end

    E1 -->|default export fn| LOADER
    E2 --> LOADER
    E3 --> LOADER
    LOADER --> RUNNER
    RUNNER -->|on ev| E1
    RUNNER -->|on ev| E2
    RUNNER -->|on ev| E3
    RUNNER -->|install| AHK
    RUNNER -->|emit/subscribe| EVT
    E1 -->|ctx.ui| UIC
    E1 -->|ctx.actions| ACTS
    RUNNER --> WRAPPER
    WRAPPER --> AGENT
```

**关键不变量**：

- 每个扩展 = 一个 `ExtensionFactory = (pi: ExtensionAPI) => void | Promise<void>`
- `ExtensionAPI` 是 Loader 注入的"自我描述"接口：扩展只能调用 `pi.xxx`，不能直接 `import` 内部模块
- 钩子触发顺序：`session_start` → `agent_start` → `turn_start` → `message_start` → `message_update*` → `message_end` → （`tool_call` → `tool_execution_*` → `tool_result`）* → `turn_end` → `agent_end` → `session_shutdown`

## 4. 资源加载架构

```mermaid
graph LR
    subgraph SRC[来源]
        S1[~/.pi/agent/]
        S2[.pi/ project local]
        S3[git URLs via<br/>pi package install]
    end

    subgraph LOADER[ResourceLoader]
        L1[loadExtensions]
        L2[loadSkills]
        L3[loadPromptTemplates]
        L4[loadThemes]
        L5[loadAgentsFiles<br/>AGENTS.md / CLAUDE.md]
    end

    subgraph REG[Registry]
        R1[ExtensionRunner]
        R2[Skill[]<br/>formatSkillsForPrompt]
        R3[PromptTemplate[]]
        R4[Theme[]<br/>selected via /theme]
        R5[ContextFile[]<br/>attached to systemPrompt]
    end

    S1 --> L1
    S2 --> L1
    S3 --> L1
    S1 --> L2
    S2 --> L2
    S3 --> L2
    S1 --> L3
    S2 --> L3
    S3 --> L3
    S1 --> L4
    S2 --> L4
    S3 --> L4
    S1 --> L5
    S2 --> L5
    L1 --> R1
    L2 --> R2
    L3 --> R3
    L4 --> R4
    L5 --> R5
    R2 --> SYSP[system-prompt.ts]
    R5 --> SYSP
    R3 --> R3B[prompt-templates.ts<br/>expandPromptTemplate]
```

**优先级**：项目级 (`.pi/`) 覆盖全局级 (`~/.pi/agent/`)；同名资源后者赢（按 `sourceInfo` 标记 `user` / `project` / `package`）。

## 5. Provider 系统架构

```mermaid
graph TB
    subgraph USER[User 视角]
        U1[--provider / --model]
        U2[/login OAuth]
        U3[auth.json]
        U4[环境变量]
    end

    subgraph REG[ModelRegistry]
        M1[built-in MODELS<br/>from pi-ai]
        M2[用户自定义 providers<br/>registerProvider / config]
        M3[resolveCliModel]
        M4[getApiKeyAndHeaders]
    end

    subgraph PI_AI[pi-ai]
        P1[streamSimple]
        P2[registerApiProvider]
        P3[registerOAuthProvider]
        P4[providers/<br/>anthropic · openai · ...]
    end

    subgraph EXT[扩展点]
        EX1[pi.registerProvider]
        EX2[pi.registerProvider oauth]
    end

    U1 --> M3
    U2 --> M3
    U3 --> M4
    U4 --> M4
    M1 --> M3
    M2 --> M3
    M3 --> P1
    P1 --> P4
    M4 --> P1
    EX1 --> M2
    EX1 --> P2
    EX2 --> P3
    P2 --> P4
    P3 --> M4
```

## 6. 三种运行模式架构

```mermaid
graph LR
    subgraph ENTRY[main.ts]
        M[AppMode resolver]
    end

    M -->|TTY + interactive| IM
    M -->|-p / --mode json| PM
    M -->|--mode rpc| RM

    subgraph IM[InteractiveMode]
        IM1[interactive-mode.ts]
        IM2[components/<br/>editor · selector · footer]
        IM3[theme/theme.ts]
    end

    subgraph PM[PrintMode]
        PM1[print-mode.ts]
        PM2[stdout writer]
    end

    subgraph RM[RpcMode]
        RM1[rpc-mode.ts]
        RM2[jsonl.ts<br/>JSONL 序列化]
        RM3[rpc-client.ts<br/>客户端示例]
    end

    IM1 -->|ctx.ui| IM2
    IM1 --> IM3
    PM1 --> PM2
    RM1 --> RM2
    RM1 --> RM3

    IM1 --> SESS[AgentSession]
    PM1 --> SESS
    RM1 --> SESS
```

**`AgentSession` 是三种模式共享的枢纽**；差异完全在 I/O 层。

## 7. 持久化架构

```mermaid
graph TB
    subgraph FILE[$AGENT_DIR/sessions/*.jsonl]
        F1[SessionHeader]
        F2[message entry]
        F3[thinking_level_change]
        F4[model_change]
        F5[compaction]
        F6[branch_summary]
        F7[label]
        F8[custom message]
        F9[custom entry]
    end

    SM[SessionManager<br/>proper-lockfile 写保护]
    AJ[AgentSession.prompt]
    SE[selectors / TUI 读]

    AJ -->|appendEntry| SM
    SE -->|readEntries / buildSessionContext| SM
    SM -->|appendFileSync| FILE
    SM -->|createReadStream| FILE
    SM -->|buildSessionContext| AJ
```

**格式版本**：`CURRENT_SESSION_VERSION = 3`，旧版本由 `migrations.ts` 自动迁移。

## 8. TUI 渲染栈

```mermaid
graph TB
    subgraph INPUT[输入]
        I1[stdin raw bytes]
        I2[StdinBuffer<br/>行/批切分]
        I3[Kitty 协议协商]
    end

    subgraph TUI[ProcessTerminal]
        T1[TUI diff 引擎]
        T2[焦点路由]
        T3[OSC 9;4 进度]
    end

    subgraph TREE[Component 树]
        C1[Container 顶层]
        C2[Editor / SelectList / Loader]
        C3[Footer / Selector 弹层]
    end

    subgraph OUT[输出]
        O1[ANSI escape]
        O2[Kitty image]
        O3[iTerm2 image]
        O4[OSC hyperlink]
        O5[OSC 11 bg]
    end

    I1 --> I2 --> I3 --> T2
    T2 --> C1 --> C2 --> C3
    C1 --> T1
    T2 --> T1
    T1 --> O1
    T1 --> O2
    T1 --> O3
    T1 --> O4
    T1 --> O5
```

## 9. 数据流总览（一次完整 prompt）

```mermaid
flowchart LR
    A([用户按键 / CLI args]) --> B[Interactive / Print / RPC]
    B --> C[AgentSession.prompt]
    C --> D[ExtensionRunner.session_start<br/>+ before_agent_start]
    D --> E[Agent.agentLoop]
    E --> F[convertToLlm]
    F --> G[streamSimple / stream]
    G --> H[Provider.stream]
    H --> I[LLM API]
    I --> H
    H --> J[AssistantMessageEventStream]
    J --> E
    E --> K[ExtensionRunner.tool_call<br/>Agent.beforeToolCall]
    K --> L[Tool.execute]
    L --> M[ExtensionRunner.tool_result<br/>Agent.afterToolCall]
    M --> N[ToolResultMessage]
    N --> E
    E --> O{shouldStop?}
    O -- no --> E
    O -- yes --> P[SessionManager.appendEntry]
    P --> Q[JSONL]
    P --> R[auto-compaction 评估]
    R -- 触发 --> S[Compactor]
    S --> Q
    R -- 不触发 --> END([emit agent_end])
    S --> END
```
