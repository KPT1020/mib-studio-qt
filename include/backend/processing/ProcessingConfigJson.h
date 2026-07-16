#pragma once

#include "backend/processing/ProcessingTypes.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace backend::processing::config_json
{

    // Qt-free (de)serialization of services::ProcessingConfig using the exact
    // `image_processing` schema of config.json (BE-3, issue #273) — the same
    // key layout the Qt ConfigTabs read/write, so existing files stay
    // compatible.
    //
    // Merge semantics: fromJson only overwrites fields that are PRESENT in the
    // JSON — absent fields keep their current values, so a partial document
    // can never silently substitute defaults. Unknown keys are ignored
    // (additive contract). Returns false with `errorOut` on malformed values.

    nlohmann::json toJson(const services::ProcessingConfig &config);

    bool fromJson(const nlohmann::json &json,
                  services::ProcessingConfig &config,
                  std::string *errorOut = nullptr);

} // namespace backend::processing::config_json
