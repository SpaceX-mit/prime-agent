// libs/tui/include/pi_tui/components/text.hpp
#pragma once

#include "pi_tui/component.hpp"
#include "pi_tui/theme.hpp"

#include <string>
#include <vector>

namespace pi::tui::components {

class Text : public Component {
public:
    Text(std::string content, std::string style = "")
        : content_(std::move(content)), style_(std::move(style)) {}

    std::vector<std::string> render(int width) const override {
        (void)width;
        std::vector<std::string> lines;
        std::string cur;
        for (char c : content_) {
            if (c == '\n') {
                lines.push_back(style_ + cur + "\x1b[0m");
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        lines.push_back(style_ + cur + "\x1b[0m");
        return lines;
    }

    void set_text(std::string s) { content_ = std::move(s); }

private:
    std::string content_;
    std::string style_;
};

}  // namespace pi::tui::components
