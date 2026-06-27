#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include <vector>

// Installs the logger. Always writes to a file at:
//   Documents/My Games/<game>/SKSE/GordianKnot.log
// where <game> is whatever SKSE::log::log_directory() resolves at runtime
// (usually "Skyrim Special Edition"; some MO2 setups resolve e.g. "Skyrim.INI",
// shared by all CommonLibSSE-NG plugins). File name comes from the plugin name.
//
// In Debug builds it ALSO emits via OutputDebugString, so log lines show up live
// in a debugger console attached to SkyrimSE.exe (e.g. CLion's "Attach to
// Process") or in Sysinternals DebugView — no need to open the file.
//
// After this runs, logger::info(...) / SKSE::log::info(...) write to all sinks.
inline void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) {
        SKSE::stl::report_and_fail("SKSE log_directory not provided; logging disabled.");
    }

    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true));
#ifndef NDEBUG
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

    auto loggerPtr = std::make_shared<spdlog::logger>("log", sinks.begin(), sinks.end());

    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::trace);
}
