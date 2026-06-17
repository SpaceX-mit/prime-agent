// pi_core/json.hpp - JSON wrapper over nlohmann/json
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace pi::core {

/// Our JSON value type — direct alias to nlohmann's ordered_json.
using Json = nlohmann::ordered_json;

/// Parse a JSON string. Throws nlohmann::json::exception on error.
/// (Callers that need a Result<T> should use tryParse instead.)
inline Json parse(std::string_view text) {
    return Json::parse(text);
}

/// Try to parse; return std::nullopt on failure.
inline std::optional<Json> tryParse(std::string_view text) noexcept {
    try {
        return Json::parse(text);
    } catch (...) {
        return std::nullopt;
    }
}

/// Serialize to a compact JSON string (no indentation).
inline std::string dump(const Json& j) {
    return j.dump();
}

/// Serialize to a pretty-printed JSON string.
inline std::string dumpPretty(const Json& j, int indent = 2) {
    return j.dump(indent);
}

}  // namespace pi::core
