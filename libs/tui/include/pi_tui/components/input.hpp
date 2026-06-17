// libs/tui/include/pi_tui/components/input.hpp
#pragma once

#include "pi_tui/component.hpp"
#include "pi_tui/theme.hpp"

#include <deque>
#include <string>
#include <vector>

namespace pi::tui::components {

class Input : public Component {
public:
    explicit Input(Theme theme);

    void set_prompt(std::string p);
    void set_text(std::string s);
    const std::string& text() const { return text_; }

    /// True if Enter was pressed since last `take_submit()`.
    bool submit_pending() const { return submit_pending_; }
    /// Consume the submit signal.
    bool take_submit();

    /// History (caller pushes after submit).
    void push_history(std::string line);
    /// Total history size.
    size_t history_size() const { return history_.size(); }

    std::vector<std::string> render(int width) const override;
    bool on_key(const KeyEvent& ev) override;

private:
    Theme theme_;
    std::string prompt_;
    std::string text_;
    size_t cursor_ = 0;
    std::deque<std::string> history_;
    int history_idx_ = -1;     // -1 = current
    std::string saved_;
    bool submit_pending_ = false;
};

}  // namespace pi::tui::components
