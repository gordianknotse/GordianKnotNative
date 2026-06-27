#pragma once

// Precompiled header for the Gordian Knot native layer.
// CommonLibSSE-NG pulls in the RE:: (reverse-engineered game) and SKSE::
// namespaces; spdlog is brought in transitively for logging.

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

using namespace std::literals;

// Convenience alias: `logger::info(...)` -> SKSE's spdlog wrapper, which routes
// to whatever default logger SetupLog() installs.
namespace logger = SKSE::log;
