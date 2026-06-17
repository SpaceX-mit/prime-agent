// libs/coding/include/pi_coding/session_manager.hpp
// Session persistence (JSONL).

#pragma once

#include "pi_ai/types.hpp"
#include "pi_core/error.hpp"
#include "pi_core/json.hpp"
#include "pi_core/result.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace pi::coding {

inline constexpr int kCurrentSessionVersion = 3;

struct SessionHeader {
    int version = kCurrentSessionVersion;
    std::string id;
    std::string timestamp;       // ISO 8601
    std::string cwd;
    std::optional<std::string> parent_session;
};

struct SessionInfo {
    std::string path;
    std::string id;
    std::string timestamp;
    std::string cwd;
    std::optional<std::string> parent_session;
    std::optional<std::string> name;
    int message_count = 0;
    int64_t file_size = 0;
};

/// SessionEntry is a discriminated union (mirrors pi-agent-core).
struct SessionEntry {
    std::string type;
    pi::core::Json data = pi::core::Json::object();

    /// Encode as a single JSON line (no trailing newline; caller adds).
    std::string encode() const;

    /// Decode a single line; returns nullopt on parse error.
    static std::optional<SessionEntry> decode(std::string_view line);

    pi::core::Json to_json() const {
        auto j = data;
        j["type"] = type;
        return j;
    }
};

class SessionManager {
public:
    /// Open (or create) a session file.
    explicit SessionManager(std::string path);

    /// Initialize the file with a header if absent.
    pi::core::Result<void> initialize(const SessionHeader& header);

    /// Read the header from the file (or nullopt if file is empty/missing).
    std::optional<SessionHeader> read_header() const;

    /// Append a session entry (one JSONL line).
    pi::core::Result<void> append_entry(const SessionEntry& entry);

    /// Read all entries.
    std::vector<SessionEntry> read_entries() const;

    /// Read all entries of a particular type.
    std::vector<SessionEntry> read_entries_of_type(const std::string& type) const;

    const std::string& path() const { return path_; }

    // --- Static helpers ---

    /// Directory where sessions live (default: $AGENT_DIR/sessions).
    static std::string default_dir();

    /// List all sessions (most recent first).
    static std::vector<SessionInfo> list_all(const std::string& dir = "");

    /// Resolve a session id prefix to a full file path.
    /// Returns empty string if no match or ambiguous.
    static std::string resolve_id_prefix(const std::string& prefix,
                                         const std::string& dir = "");

    /// Generate a new session id (e.g. "01JABCDEF...").
    static std::string new_session_id();

private:
    std::string path_;
};

// ---------------------------------------------------------------------------
// SessionContext: the assembled view of a session (entries → LLM messages)
// ---------------------------------------------------------------------------

struct SessionContext {
    SessionHeader header;
    std::vector<pi::ai::Message> messages;
    int last_compaction_index = -1;
    int last_branch_summary_index = -1;
};

/// Build a SessionContext from a header + entries.
SessionContext build_session_context(const SessionHeader& header,
                                     const std::vector<SessionEntry>& entries,
                                     bool exclude_system_reminder = false);

}  // namespace pi::coding
