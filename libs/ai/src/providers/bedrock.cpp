// libs/ai/src/providers/bedrock.cpp
// AWS Bedrock Converse Stream provider.
// Auth: AWS SigV4 (HMAC-SHA256) from env: AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY,
//       AWS_SESSION_TOKEN (optional), AWS_DEFAULT_REGION / AWS_REGION.
// Protocol: POST /model/<modelId>/converse-stream → application/vnd.amazon.eventstream
//           (binary framing: 4-byte total_len + 4-byte headers_len + 4-byte prelude_crc32
//           + headers + payload + 4-byte message_crc32).
#include "pi_ai/providers/bedrock.hpp"
#include "pi_ai/event_stream.hpp"
#include "pi_core/env.hpp"
#include "pi_core/json.hpp"
#include "pi_http/http_client.hpp"

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <thread>

namespace pi::ai::providers {

namespace {

// ---------------------------------------------------------------------------
// SigV4 helpers
// ---------------------------------------------------------------------------

std::string hex(const unsigned char* d, size_t n) {
    std::ostringstream o; o << std::hex << std::setfill('0');
    for (size_t i = 0; i < n; ++i) o << std::setw(2) << (int)d[i];
    return o.str();
}

std::string sha256_hex(const std::string& s) {
    unsigned char h[32];
    SHA256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), h);
    return hex(h, 32);
}

std::vector<unsigned char> hmac_sha256(const std::vector<unsigned char>& key,
                                        const std::string& msg) {
    unsigned char out[32]; unsigned int len = 32;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(msg.data()), msg.size(), out, &len);
    return std::vector<unsigned char>(out, out + len);
}

std::vector<unsigned char> hmac_sha256(const std::string& key, const std::string& msg) {
    return hmac_sha256(std::vector<unsigned char>(key.begin(), key.end()), msg);
}

// AWS SigV4 signing. Returns the Authorization header value.
std::string sigv4_auth(const std::string& method, const std::string& uri,
                       const std::string& body, const std::string& host,
                       const std::string& region, const std::string& service,
                       const std::string& access_key, const std::string& secret_key,
                       const std::string& session_token,
                       std::string& out_amz_date) {
    // Date/time.
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{}; gmtime_r(&t, &tm);
    char date_buf[9], datetime_buf[17];
    strftime(date_buf,     sizeof(date_buf),     "%Y%m%d",       &tm);
    strftime(datetime_buf, sizeof(datetime_buf), "%Y%m%dT%H%M%SZ", &tm);
    std::string date(date_buf), datetime(datetime_buf);
    out_amz_date = datetime;

    const std::string body_hash = sha256_hex(body);
    const std::string content_type = "application/json";

    // Canonical headers (sorted).
    std::string canon_headers = "content-type:" + content_type + "\n"
                              + "host:" + host + "\n"
                              + "x-amz-content-sha256:" + body_hash + "\n"
                              + "x-amz-date:" + datetime + "\n";
    if (!session_token.empty())
        canon_headers += "x-amz-security-token:" + session_token + "\n";

    std::string signed_headers = "content-type;host;x-amz-content-sha256;x-amz-date";
    if (!session_token.empty()) signed_headers += ";x-amz-security-token";

    const std::string canon_req = method + "\n" + uri + "\n\n"
        + canon_headers + "\n" + signed_headers + "\n" + body_hash;
    const std::string scope = std::string(date) + "/" + region + "/" + service + "/aws4_request";
    const std::string sts = "AWS4-HMAC-SHA256\n" + datetime + "\n" + scope
                          + "\n" + sha256_hex(canon_req);

    // Signing key.
    auto k1 = hmac_sha256(std::string("AWS4") + secret_key, date);
    auto k2 = hmac_sha256(k1, region);
    auto k3 = hmac_sha256(k2, service);
    auto k4 = hmac_sha256(k3, "aws4_request");
    auto sig_bytes = hmac_sha256(k4, sts);
    const std::string sig = hex(sig_bytes.data(), sig_bytes.size());

    return "AWS4-HMAC-SHA256 Credential=" + access_key + "/" + scope
         + ", SignedHeaders=" + signed_headers + ", Signature=" + sig;
}

// ---------------------------------------------------------------------------
// Amazon Event Stream binary frame parser
// ---------------------------------------------------------------------------

struct EvFrame { std::string type; std::string payload; };

// Parse all complete frames from `buf`, emitting via `on_frame`.
// Leaves incomplete trailing bytes in buf.
uint32_t crc32_ieee(const uint8_t* d, size_t n) {
    // CRC-32 (ISO 3309) table-driven, built once at first call.
    static uint32_t kTable[256] = {};
    static bool kInit = false;
    if (!kInit) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            kTable[i] = c;
        }
        kInit = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i)
        crc = kTable[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void parse_frames(std::string& buf,
                  const std::function<void(const EvFrame&)>& on_frame) {
    while (buf.size() >= 12) {
        auto* p = reinterpret_cast<const uint8_t*>(buf.data());
        uint32_t total_len  = ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
        uint32_t header_len = ((uint32_t)p[4]<<24)|((uint32_t)p[5]<<16)|((uint32_t)p[6]<<8)|p[7];
        if (buf.size() < total_len) break;  // incomplete frame

        // Parse headers to find ":event-type".
        std::string event_type;
        size_t hi = 12, he = 12 + header_len;
        while (hi < he) {
            if (hi >= buf.size()) break;
            uint8_t name_len = p[hi++];
            if (hi + name_len > he) break;
            std::string name(buf.data() + hi, name_len); hi += name_len;
            if (hi >= he) break;
            /*uint8_t type =*/ p[hi++];  // value type (always 7 = string here)
            if (hi + 2 > he) break;
            uint16_t val_len = ((uint16_t)p[hi]<<8)|p[hi+1]; hi += 2;
            if (hi + val_len > he) break;
            std::string val(buf.data() + hi, val_len); hi += val_len;
            if (name == ":event-type" || name == ":exception-type") event_type = val;
        }

        // Payload sits between headers and the trailing 4-byte CRC.
        size_t payload_start = 12 + header_len;
        size_t payload_len   = total_len >= payload_start + 4
            ? total_len - payload_start - 4 : 0;
        std::string payload(buf.data() + payload_start, payload_len);

        on_frame(EvFrame{event_type, payload});
        buf.erase(0, total_len);
    }
}

// ---------------------------------------------------------------------------
// Converse message builder
// ---------------------------------------------------------------------------

pi::core::Json build_converse_body(const Context& ctx, const StreamOptions& opts) {
    pi::core::Json body;
    if (opts.max_tokens) body["inferenceConfig"]["maxTokens"] = *opts.max_tokens;
    if (opts.temperature) body["inferenceConfig"]["temperature"] = *opts.temperature;

    if (ctx.system_prompt && !ctx.system_prompt->empty())
        body["system"] = pi::core::Json::array({{{"text", *ctx.system_prompt}}});

    pi::core::Json msgs = pi::core::Json::array();
    for (auto& m : ctx.messages) {
        std::visit([&](auto& mm) {
            using T = std::decay_t<decltype(mm)>;
            if constexpr (std::is_same_v<T, UserMessage>) {
                pi::core::Json j{{"role","user"}};
                pi::core::Json content = pi::core::Json::array();
                for (auto& c : mm.content)
                    if (std::holds_alternative<TextContent>(c))
                        content.push_back({{"text", std::get<TextContent>(c).text}});
                j["content"] = std::move(content);
                msgs.push_back(std::move(j));
            } else if constexpr (std::is_same_v<T, AssistantMessage>) {
                pi::core::Json j{{"role","assistant"}};
                pi::core::Json content = pi::core::Json::array();
                for (auto& c : mm.content)
                    if (std::holds_alternative<TextContent>(c))
                        content.push_back({{"text", std::get<TextContent>(c).text}});
                j["content"] = std::move(content);
                msgs.push_back(std::move(j));
            } else {
                pi::core::Json j{{"role","user"}};
                std::string out;
                for (auto& c : mm.content)
                    if (std::holds_alternative<TextContent>(c))
                        out += std::get<TextContent>(c).text;
                j["content"] = pi::core::Json::array(
                    {{{{"toolResult", {{"toolUseId", mm.tool_call_id}, {"content", pi::core::Json::array({{{{"text", out}}}})}}}}}});
                msgs.push_back(std::move(j));
            }
        }, m);
    }
    body["messages"] = std::move(msgs);
    return body;
}

}  // namespace

std::shared_ptr<EventStream> BedrockConverseProvider::stream(
    const Model& model, const Context& ctx, const StreamOptions& opts) {
    auto out = std::make_shared<EventStream>();
    std::thread([out, model, ctx, opts]() mutable {
        // Credentials from env.
        auto ak = pi::core::env::get("AWS_ACCESS_KEY_ID");
        auto sk = pi::core::env::get("AWS_SECRET_ACCESS_KEY");
        auto reg_env = pi::core::env::get("AWS_DEFAULT_REGION");
        if (!reg_env) reg_env = pi::core::env::get("AWS_REGION");
        std::string session_token;
        if (auto st = pi::core::env::get("AWS_SESSION_TOKEN")) session_token = *st;

        if (!ak || ak->empty() || !sk || sk->empty()) {
            AssistantMessage m; m.stop_reason = "error";
            m.error_message = "bedrock: set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY";
            out->end(std::move(m)); return;
        }
        std::string region = (reg_env && !reg_env->empty()) ? *reg_env : "us-east-1";
        // Use model.base_url as region override if it looks like a region.
        if (!model.base_url.empty() && model.base_url.find('.') == std::string::npos)
            region = model.base_url;

        const std::string host = "bedrock-runtime." + region + ".amazonaws.com";
        const std::string uri  = "/model/" + model.id + "/converse-stream";

        std::string body_str = build_converse_body(ctx, opts).dump();
        std::string amz_date;
        std::string auth_header = sigv4_auth("POST", uri, body_str, host, region,
            "bedrock", *ak, *sk, session_token, amz_date);

        AssistantMessage partial;
        partial.api = partial.provider = "bedrock";
        partial.model = model.id;
        out->push(AssistantMessageEvent::start(partial));

        int text_idx = 0; bool text_started = false;
        std::string frame_buf;

        pi::http::HttpClient client;
        pi::http::HttpRequest req;
        req.method = "POST";
        req.url    = "https://" + host + uri;
        req.body   = body_str;
        req.timeout_ms = opts.timeout_ms;
        req.headers["Content-Type"]          = "application/json";
        req.headers["Accept"]                = "application/vnd.amazon.eventstream";
        req.headers["host"]                  = host;
        req.headers["x-amz-date"]            = amz_date;
        req.headers["x-amz-content-sha256"]  = sha256_hex(body_str);
        req.headers["Authorization"]         = auth_header;
        if (!session_token.empty())
            req.headers["x-amz-security-token"] = session_token;

        auto res = client.send_streaming(req, [&](const pi::http::HttpChunk& chunk) -> bool {
            frame_buf += chunk.data;
            parse_frames(frame_buf, [&](const EvFrame& f) {
                auto j = pi::core::tryParse(f.payload);
                if (!j) return;
                if (f.type == "contentBlockDelta") {
                    std::string delta = (*j)
                        .value("delta", pi::core::Json::object())
                        .value("text", std::string{});
                    if (delta.empty()) return;
                    if (!text_started) {
                        out->push(AssistantMessageEvent::text_start(text_idx, partial));
                        partial.content.push_back(TextContent{});
                        text_started = true;
                    }
                    std::get<TextContent>(partial.content.back()).text += delta;
                    out->push(AssistantMessageEvent::text_delta(text_idx, delta, partial));
                } else if (f.type == "messageStop") {
                    if (text_started)
                        out->push(AssistantMessageEvent::text_end(text_idx,
                            std::get<TextContent>(partial.content.back()).text, partial));
                    partial.stop_reason = (*j).value("stopReason", std::string{"end_turn"});
                    if (partial.stop_reason == "end_turn") partial.stop_reason = "stop";
                    out->push(AssistantMessageEvent::done(partial.stop_reason, partial));
                } else if (f.type == "metadata") {
                    auto& usage = (*j)["usage"];
                    if (usage.is_object()) {
                        partial.usage["input_tokens"]  = usage.value("inputTokens",  0);
                        partial.usage["output_tokens"] = usage.value("outputTokens", 0);
                    }
                }
            });
            return true;
        });

        if (!res || !res.value().ok()) {
            partial.stop_reason = "error";
            partial.error_message = res
                ? "bedrock: HTTP " + std::to_string(res.value().status)
                : "bedrock: " + res.error().to_string();
        }
        if (partial.stop_reason.empty()) partial.stop_reason = "stop";
        out->end(std::move(partial));
    }).detach();
    return out;
}

// Self-register.
namespace { struct BedReg { BedReg() {
    ProviderRegistry::instance().register_provider(std::make_shared<BedrockConverseProvider>());
} } kBedReg; }

}  // namespace pi::ai::providers
