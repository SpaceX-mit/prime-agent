// libs/coding/src/session_manager.cpp
#include "pi_coding/session_manager.hpp"

#include "pi_core/file_io.hpp"
#include "pi_core/lockfile.hpp"
#include "pi_core/path.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <iomanip>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace pi::coding {

namespace {

std::string env_dir() {
    if (auto v = std::getenv("PI_AGENT_DIR"); v && *v) return v;
    if (auto h = pi::core::path::home_dir(); h) return *h + "/.pi/agent";
    return "/tmp/.pi/agent";
}

std::string iso8601_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string gen_random_hex(size_t bytes) {
    static thread_local std::mt19937_64 rng{
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    std::ostringstream o;
    o << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes; ++i) {
        o << std::setw(2) << (rng() & 0xff);
    }
    return o.str();
}

}  // namespace

std::string SessionEntry::encode() const {
    auto j = to_json();
    return j.dump();
}

std::optional<SessionEntry> SessionEntry::decode(std::string_view line) {
    auto j = pi::core::tryParse(line);
    if (!j || !j->is_object() || !j->contains("type")) return std::nullopt;
    SessionEntry e;
    e.type = (*j)["type"].get<std::string>();
    e.data = *j;
    e.data.erase("type");
    return e;
}

SessionManager::SessionManager(std::string path) : path_(std::move(path)) {}

std::string SessionManager::default_dir() {
    return env_dir() + "/sessions";
}

std::string SessionManager::new_session_id() {
    // ULID-ish: 26 chars, base32.
    auto t = std::chrono::system_clock::now().time_since_epoch();
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
    static const char* kAlpha = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    std::string id;
    id.reserve(26);
    // First 10 chars: timestamp
    for (int i = 9; i >= 0; --i) {
        id.push_back(kAlpha[(ms >> (i * 5)) & 0x1f]);
    }
    // Next 16 chars: random
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    for (int i = 0; i < 16; ++i) {
        id.push_back(kAlpha[rng() & 0x1f]);
    }
    return id;
}

pi::core::Result<void> SessionManager::initialize(const SessionHeader& header) {
    if (pi::core::file::exists(path_)) {
        // Already exists; nothing to do.
        return pi::core::Result<void>::ok();
    }
    auto parent = std::string();
    {
        auto p = path_;
        auto slash = p.find_last_of('/');
        if (slash != std::string::npos) parent = p.substr(0, slash);
    }
    if (!parent.empty()) {
        auto r = pi::core::file::create_directories(parent);
        if (!r) return r.error();
    }
    pi::core::Json j;
    j["type"] = "session_header";
    j["version"] = header.version;
    j["id"] = header.id;
    j["timestamp"] = header.timestamp;
    j["cwd"] = header.cwd;
    if (header.parent_session) j["parentSession"] = *header.parent_session;
    auto w = pi::core::file::write_atomic(path_, j.dump() + "\n");
    return w;
}

std::optional<SessionHeader> SessionManager::read_header() const {
    auto r = pi::core::file::read(path_);
    if (!r) return std::nullopt;
    std::string_view text = r.value();
    if (text.empty()) return std::nullopt;
    auto nl = text.find('\n');
    if (nl != std::string::npos) text = text.substr(0, nl);
    auto j = pi::core::tryParse(text);
    if (!j || !j->is_object()) return std::nullopt;
    if (j->value("type", std::string{}) != "session_header") return std::nullopt;
    SessionHeader h;
    h.version = j->value("version", kCurrentSessionVersion);
    h.id = j->value("id", std::string{});
    h.timestamp = j->value("timestamp", std::string{});
    h.cwd = j->value("cwd", std::string{});
    if (j->contains("parentSession") && (*j)["parentSession"].is_string()) {
        h.parent_session = (*j)["parentSession"].get<std::string>();
    }
    return h;
}

pi::core::Result<void> SessionManager::append_entry(const SessionEntry& entry) {
    std::string line = entry.encode() + "\n";
    pi::core::lockfile::FileLock lk(path_);
    auto guard = lk.acquire(std::chrono::seconds(5));
    if (!guard) {
        return pi::core::Error{pi::core::ErrorKind::Timeout,
                                "session: could not acquire lock on " + path_};
    }
    auto a = pi::core::file::append(path_, line);
    return a;
}

std::vector<SessionEntry> SessionManager::read_entries() const {
    std::vector<SessionEntry> out;
    auto r = pi::core::file::read(path_);
    if (!r) return out;
    std::string_view text = r.value();
    size_t pos = 0;
    while (pos < text.size()) {
        auto nl = text.find('\n', pos);
        std::string_view line;
        if (nl == std::string::npos) {
            line = text.substr(pos);
            pos = text.size();
        } else {
            line = text.substr(pos, nl - pos);
            pos = nl + 1;
        }
        if (line.empty()) continue;
        auto e = SessionEntry::decode(line);
        if (e) out.push_back(std::move(*e));
    }
    return out;
}

std::vector<SessionEntry> SessionManager::read_entries_of_type(const std::string& type) const {
    auto all = read_entries();
    std::vector<SessionEntry> out;
    for (auto& e : all) {
        if (e.type == type) out.push_back(std::move(e));
    }
    return out;
}

std::vector<SessionInfo> SessionManager::list_all(const std::string& dir_in) {
    std::string dir = dir_in.empty() ? default_dir() : dir_in;
    std::vector<SessionInfo> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    struct dirent* ent;
    while ((ent = ::readdir(d))) {
        std::string name = ent->d_name;
        if (name.size() < 6 || name.substr(name.size() - 6) != ".jsonl") continue;
        std::string full = dir + "/" + name;
        SessionInfo info;
        info.path = full;
        info.file_size = pi::core::file::size(full);
        // Read first line for header.
        auto r = pi::core::file::read(full);
        if (!r) continue;
        std::string_view text = r.value();
        auto nl = text.find('\n');
        if (nl != std::string::npos) text = text.substr(0, nl);
        auto j = pi::core::tryParse(text);
        if (!j || j->value("type", std::string{}) != "session_header") continue;
        info.id = j->value("id", name.substr(0, name.size() - 5));
        info.timestamp = j->value("timestamp", std::string{});
        info.cwd = j->value("cwd", std::string{});
        if (j->contains("parentSession")) info.parent_session = (*j)["parentSession"].get<std::string>();
        // Find session_name.
        auto nl2 = r.value().find('\n');
        std::string_view rest = (nl2 == std::string::npos) ? std::string_view{} : std::string_view(r.value()).substr(nl2 + 1);
        // Count message lines + look for session_name.
        size_t line_no = 0;
        int msg_count = 0;
        size_t cursor = 0;
        while (cursor < rest.size()) {
            auto e = rest.find('\n', cursor);
            std::string_view l = (e == std::string_view::npos) ? rest.substr(cursor) : rest.substr(cursor, e - cursor);
            cursor = (e == std::string_view::npos) ? rest.size() : e + 1;
            line_no++;
            auto j2 = pi::core::tryParse(l);
            if (!j2) continue;
            std::string t = j2->value("type", std::string{});
            if (t == "message") msg_count++;
            if (t == "session_info" && j2->contains("name")) {
                info.name = (*j2)["name"].get<std::string>();
            }
        }
        info.message_count = msg_count;
        out.push_back(std::move(info));
    }
    ::closedir(d);
    // Sort by timestamp desc.
    std::sort(out.begin(), out.end(),
              [](const SessionInfo& a, const SessionInfo& b) { return a.timestamp > b.timestamp; });
    return out;
}

std::string SessionManager::resolve_id_prefix(const std::string& prefix,
                                              const std::string& dir_in) {
    std::string dir = dir_in.empty() ? default_dir() : dir_in;
    auto all = list_all(dir);
    std::vector<std::string> matches;
    for (auto& info : all) {
        if (info.id.size() >= prefix.size() && info.id.compare(0, prefix.size(), prefix) == 0) {
            matches.push_back(info.path);
        }
    }
    if (matches.size() != 1) return "";
    return matches[0];
}

SessionContext build_session_context(const SessionHeader& header,
                                     const std::vector<SessionEntry>& entries,
                                     bool exclude_system_reminder) {
    SessionContext ctx;
    ctx.header = header;
    for (auto& e : entries) {
        if (e.type == "message") {
            auto& m = e.data["message"];
            std::string role = m.value("role", std::string{});
            if (role == "user") {
                pi::ai::UserMessage um;
                if (m.contains("content")) {
                    auto& content = m["content"];
                    if (content.is_string()) {
                        um.content.push_back(pi::ai::TextContent{content.get<std::string>()});
                    } else if (content.is_array()) {
                        for (auto& c : content) {
                            std::string ct = c.value("type", std::string{});
                            if (ct == "text") {
                                um.content.push_back(pi::ai::TextContent{c.value("text", std::string{})});
                            }
                        }
                    }
                }
                ctx.messages.push_back(um);
            } else if (role == "assistant") {
                pi::ai::AssistantMessage am;
                am.role = "assistant";
                if (m.contains("content") && m["content"].is_array()) {
                    for (auto& c : m["content"]) {
                        std::string ct = c.value("type", std::string{});
                        if (ct == "text") {
                            am.content.push_back(pi::ai::TextContent{c.value("text", std::string{})});
                        } else if (ct == "toolCall") {
                            pi::ai::ToolCall tc;
                            tc.id = c.value("id", std::string{});
                            tc.name = c.value("name", std::string{});
                            if (c.contains("arguments")) {
                                tc.arguments_json = c["arguments"].dump();
                            }
                            am.content.push_back(tc);
                        }
                    }
                }
                am.stop_reason = m.value("stopReason", std::string{"stop"});
                if (m.contains("usage")) am.usage = m["usage"];
                if (m.contains("api")) am.api = m["api"].get<std::string>();
                if (m.contains("provider")) am.provider = m["provider"].get<std::string>();
                if (m.contains("model")) am.model = m["model"].get<std::string>();
                ctx.messages.push_back(am);
            } else if (role == "toolResult") {
                pi::ai::ToolResultMessage tr;
                tr.tool_call_id = m.value("toolCallId", std::string{});
                tr.tool_name = m.value("toolName", std::string{});
                tr.is_error = m.value("isError", false);
                if (m.contains("content") && m["content"].is_array()) {
                    for (auto& c : m["content"]) {
                        std::string ct = c.value("type", std::string{});
                        if (ct == "text") {
                            tr.content.push_back(pi::ai::TextContent{c.value("text", std::string{})});
                        }
                    }
                }
                ctx.messages.push_back(tr);
            }
        } else if (e.type == "compaction") {
            // For V1 we don't actually compact; we just track the index.
            ctx.last_compaction_index = static_cast<int>(ctx.messages.size());
        }
    }
    (void)exclude_system_reminder;
    return ctx;
}

}  // namespace pi::coding
