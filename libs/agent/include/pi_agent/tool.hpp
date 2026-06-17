// libs/agent/include/pi_agent/tool.hpp
// Agent-level tool abstraction.
//
// Tool is the C++ equivalent of the TS `AgentTool` interface in pi-agent-core.
// Each tool has a name, description, JSON Schema for parameters, and an
// `execute` method that performs the work.

#pragma once

#include "pi_ai/event_stream.hpp"
#include "pi_ai/types.hpp"
#include "pi_core/error.hpp"
#include "pi_core/json.hpp"

#include <functional>
#include <memory>
#include <string>

namespace pi::agent {

/// Progress update callback (used for streaming partial tool results).
using ProgressFn = std::function<void(const pi::ai::Content&)>;

/// Abort signal — minimal interface (TS AbortSignal is more elaborate).
class AbortSignal {
public:
    virtual ~AbortSignal() = default;
    virtual bool aborted() const = 0;
    virtual void throw_if_aborted() const {
        if (aborted()) throw pi::core::InternalError("aborted");
    }
};

/// Always-not-aborted signal.
class NullAbort : public AbortSignal {
public:
    bool aborted() const override { return false; }
};

/// Tool execution result.
struct ToolResult {
    std::vector<pi::ai::Content> content;
    pi::core::Json details = pi::core::Json::object();
    bool is_error = false;
};

/// Tool interface.
class Tool {
public:
    virtual ~Tool() = default;

    /// Identifier passed to the LLM.
    virtual std::string name() const = 0;
    /// Human label (for UI rendering).
    virtual std::string label() const = 0;
    /// Description sent to the LLM.
    virtual std::string description() const = 0;
    /// JSON Schema for parameters.
    virtual pi::core::Json parameters() const = 0;

    /// Execute the tool.
    /// `args` is the already-parsed JSON object.
    virtual ToolResult execute(
        const pi::core::Json& args,
        AbortSignal& signal,
        ProgressFn on_update) = 0;

    /// Convert this tool to a ToolSpec for LLM function-calling.
    pi::ai::ToolSpec to_spec() const {
        pi::ai::ToolSpec s;
        s.name = name();
        s.description = description();
        s.parameters = parameters();
        return s;
    }
};

using ToolPtr = std::shared_ptr<Tool>;

}  // namespace pi::agent
