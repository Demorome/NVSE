#pragma once

#include "EventManager.h"
#include "json.hpp"
#include <filesystem>
#include <fstream>

// for convenience
namespace fs = std::filesystem;
using json = nlohmann::json;

void ReadEventDefinitionsFromFile()
{
    const fs::path path = fs::path{ fs::current_path() } / "data/nvse/event_definitions";
    for (const auto& entry : fs::directory_iterator(path)) {
        const auto filenameStr = entry.path().filename().string();
		if (entry.is_regular_file()) {
            std::fstream file(filenameStr);
            try
            {
                json jf = json::parse(file, nullptr, true, true);
                
            }
            catch (const json::parse_error& e)
            {
                _ERROR("Invalid JSON file in nvse/event_definitions, error: %s", e.what());
                //TODO: console_print?
            }
        }
    }
}
