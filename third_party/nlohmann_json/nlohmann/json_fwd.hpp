// The vendored library is the single header only. Headers that just name nlohmann::json include this file so the
// compiler never picks up a different system copy of json_fwd.hpp (that mixes two versions in one translation unit).
#pragma once
#include <nlohmann/json.hpp>
