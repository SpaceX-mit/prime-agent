// libs/tui/include/pi_tui/components/footer.hpp
#pragma once

#include "pi_tui/component.hpp"
#include "pi_tui/theme.hpp"

#include <string>

namespace pi::tui::components {

class Footer : public Component {
public:
    Footer(Theme theme, std::string mode, std::string model);

    void set_mode(std::string s);
    void set_model(std::string s);
    void set_status(std::string s);
    void set_tokens(int64_t in, int64_t out);

    std::vector<std::string> render(int width) const override;

private:
    Theme theme_;
    std::string mode_;
    std::string model_;
    std::string status_;
    int64_t tokens_in_ = 0;
    int64_t tokens_out_ = 0;
};

}  // namespace pi::tui::components
