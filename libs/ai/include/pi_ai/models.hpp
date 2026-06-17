// libs/ai/include/pi_ai/models.hpp
// Built-in model registry. The TS version generates this from a script;
// here we hand-write a small set of models covering the major providers.

#pragma once

#include "types.hpp"
#include <vector>

namespace pi::ai {

/// Built-in models.
const std::vector<Model>& builtin_models();

/// Built-in Anthropic models.
const std::vector<Model>& anthropic_models();
const std::vector<Model>& openai_models();

/// Find a model by "<provider>/<id>".
const Model* find_model(const std::string& key);

/// List all unique provider names.
std::vector<std::string> provider_names();

}  // namespace pi::ai
