// libs/tui/src/think_filter.cpp
#include "pi_tui/think_filter.hpp"

namespace pi::tui {

void ThinkFilter::feed(const std::string& delta,
                       const std::string& enter,
                       const std::string& leave,
                       std::string& out) {
    buf += delta;
    while (true) {
        if (!in_think) {
            auto p = buf.find("<think>");
            if (p == std::string::npos) {
                // Hold any trailing fragment that could still grow into
                // "<think>" (i.e. a '<' within the last 6 chars).
                size_t lt = buf.find_last_of('<');
                size_t safe = (lt != std::string::npos && buf.size() - lt < 7)
                                ? lt : buf.size();
                out.append(buf, 0, safe);
                buf.erase(0, safe);
                return;
            }
            out.append(buf, 0, p);
            buf.erase(0, p + 7);
            in_think = true;
            out += enter;
        } else {
            auto p = buf.find("</think>");
            if (p == std::string::npos) {
                size_t lt = buf.find_last_of('<');
                size_t safe = (lt != std::string::npos && buf.size() - lt < 8)
                                ? lt : buf.size();
                out.append(buf, 0, safe);
                buf.erase(0, safe);
                return;
            }
            out.append(buf, 0, p);
            out += leave;
            buf.erase(0, p + 8);
            in_think = false;
        }
    }
}

}  // namespace pi::tui
