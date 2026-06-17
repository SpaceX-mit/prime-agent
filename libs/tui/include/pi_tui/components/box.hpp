// libs/tui/include/pi_tui/components/box.hpp
#pragma once

#include "pi_tui/component.hpp"

#include <memory>
#include <vector>

namespace pi::tui::components {

class Box : public Component {
public:
    enum Direction { Vertical, Horizontal };

    Box(Direction dir = Vertical, int gap = 0)
        : dir_(dir), gap_(gap) {}

    void add(ComponentPtr child) { children_.push_back(std::move(child)); }

    std::vector<std::string> render(int width) const override {
        std::vector<std::string> out;
        bool first = true;
        for (auto& c : children_) {
            if (!first && gap_ > 0) {
                if (dir_ == Vertical) out.push_back(std::string(gap_, ' '));
                else {
                    if (!out.empty()) out.back() += std::string(gap_, ' ');
                }
            }
            auto lines = c->render(width);
            if (dir_ == Vertical) {
                for (auto& l : lines) out.push_back(l);
            } else {
                if (out.empty()) out.push_back("");
                for (size_t i = 0; i < lines.size(); ++i) {
                    if (i + 1 > out.size()) out.push_back("");
                    if (i == 0) out.back() += lines[i];
                    else out[i] += lines[i];
                }
            }
            first = false;
        }
        if (out.empty()) out.push_back("");
        return out;
    }

private:
    Direction dir_;
    int gap_;
    std::vector<ComponentPtr> children_;
};

}  // namespace pi::tui::components
