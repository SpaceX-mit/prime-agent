// libs/coding/include/pi_coding/modes/rpc.hpp
// RPC mode: line-delimited JSON over stdin/stdout.

#pragma once

#include "pi_ai/models.hpp"
#include "pi_ai/types.hpp"

#include <functional>
#include <optional>
#include <string>

namespace pi::coding::modes {

/// Run RPC mode: read commands from stdin (line-delimited JSON),
/// write responses + events to stdout.
///
/// Returns 0 on clean shutdown, non-zero on fatal error.
int run_rpc_mode(const pi::ai::Model& model,
                 pi::ai::SimpleStreamOptions opts,
                 std::string cwd,
                 std::function<std::string(const std::string&)> api_key_resolver);

}  // namespace pi::coding::modes
