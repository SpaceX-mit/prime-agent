// libs/tui/src/components/footer.cpp
#include "pi_tui/components/footer.hpp"

#include <sstream>

namespace pi::tui::components {

Footer::Footer(Theme theme, std::string mode, std::string model)
    : theme_(std::move(theme)), mode_(std::move(mode)), model_(std::move(model)) {}

void Footer::set_mode(std::string s) { mode_ = std::move(s); }
void Footer::set_model(std::string s) { model_ = std::move(s); }
void Footer::set_status(std::string s) { status_ = std::move(s); }
void Footer::set_tokens(int64_t in, int64_t out) { tokens_in_ = in; tokens_out_ = out; }

std::vector<std::string> Footer::render(int width) const {
    std::ostringstream o;
    o << theme_.footer_bg << theme_.footer_fg;
    std::string left = " " + mode_ + "  " + (model_.empty() ? "" : "model: " + model_);
    std::string right;
    if (!status_.empty()) right += status_ + "  ";
    if (tokens_in_ || tokens_out_) {
        right += "in:" + std::to_string(tokens_in_) + " out:" + std::to_string(tokens_out_);
    }
    if (static_cast<int>(left.size() + right.size()) > width) {
        // Truncate left.
        if (static_cast<int>(left.size()) > width - 3) left = left.substr(0, width - 3) + "...";
        right.clear();
    }
    int pad = width - static_cast<int>(left.size()) - static_cast<int>(right.size());
    if (pad < 1) pad = 1;
    o << left << std::string(pad, ' ') << right << "\x1b[0m";
    return {o.str()};
}

}  // namespace pi::tui::components
