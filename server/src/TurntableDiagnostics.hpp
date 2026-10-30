#pragma once

#include <string>

namespace TurntableDiagnostics
{
    void Log(const std::string& component, const std::string& message);
    void Record(const std::string& component, const std::string& event, const std::string& details);
}
