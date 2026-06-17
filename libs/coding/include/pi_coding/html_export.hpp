// libs/coding/include/pi_coding/html_export.hpp
// Standalone HTML export of a session.

#pragma once

#include "pi_coding/session_manager.hpp"

#include <string>

namespace pi::coding {

/// Export a session file (JSONL) to a standalone HTML file.
/// Returns the number of messages written, or -1 on error.
int export_session_html(const std::string& jsonl_path,
                        const std::string& html_path);

}  // namespace pi::coding
