// libs/tui/include/pi_tui/component.hpp
// Component base class for the TUI tree.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pi::tui {

/// A render hint: minimum width needed for the component to look good.
struct Constraints {
    int min_width = 0;
    int min_height = 0;
};

/// Base class for all UI components. Components are immutable: they
/// receive new state and re-render.
class Component {
public:
    virtual ~Component() = default;

    /// Render the component into a list of `rows` lines, each of width `width`.
    /// The component is responsible for padding/truncation to fit.
    virtual std::vector<std::string> render(int width) const = 0;

    /// Optional: minimum size hint.
    virtual Constraints constraints() const { return {}; }

    /// Optional: handle a key event when this component is focused.
    /// Return true if the event was consumed.
    virtual bool on_key(const struct KeyEvent&) { return false; }
};

using ComponentPtr = std::shared_ptr<Component>;

}  // namespace pi::tui
