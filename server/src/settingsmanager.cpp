#include "settingsmanager.h"

#include "adb.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
    double ReadDoubleSetting(
        const std::map<std::string, std::string>& settings,
        const std::string& key,
        double defaultValue,
        double minimum,
        double maximum)
    {
        const auto setting = settings.find(key);
        if (setting == settings.end())
            return defaultValue;

        char* end = nullptr;
        const double value = std::strtod(setting->second.c_str(), &end);
        if (end == setting->second.c_str() || *end != '\0' || !std::isfinite(value))
        {
            LOG("[SETTINGS] Invalid numeric value for ", key, "; using default.");
            return defaultValue;
        }
        return std::clamp(value, minimum, maximum);
    }

    int ReadIntegerSetting(
        const std::map<std::string, std::string>& settings,
        const std::string& key,
        int defaultValue,
        int minimum,
        int maximum)
    {
        return static_cast<int>(ReadDoubleSetting(settings, key, defaultValue, minimum, maximum));
    }
}

SettingsManager::SettingsManager() : bAdbStarted(false)
{
    #ifdef _WIN32
        char buff[128] = { 0 };
        DWORD buffSize = sizeof(buff);

        LONG s2 = RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"Mousedroid", RRF_RT_REG_SZ, nullptr, &buff, &buffSize);
        
        settings["RUN_STARTUP"] = std::to_string((s2 == ERROR_SUCCESS));
    #endif

    LoadConfigs();
}

void SettingsManager::LoadConfigs()
{
    std::ifstream fin("config.ini");

    std::string line;
    while(std::getline(fin, line))
    {
        size_t pos = line.find('=');
        std::string setting = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        settings[setting] = value; 
    }

    fin.close();

    InputManager::MOVE_SENSITIVITY = std::atoi(settings["MOVE_SENSITIVITY"].c_str());
    InputManager::SCROLL_SENSITIVITY = std::atoi(settings["SCROLL_SENSITIVITY"].c_str());

    turntableSettings.acceleration = ReadDoubleSetting(settings, "TT_ACCELERATION", 1.0, 0.1, 5.0);
    turntableSettings.maximumVelocityPercent =
        ReadIntegerSetting(settings, "TT_MAX_VELOCITY", 100, 10, 200);
    turntableSettings.coastDurationMs =
        ReadIntegerSetting(settings, "TT_COAST_DURATION_MS", 1000, 100, 5000);
    turntableSettings.sensitivity = ReadDoubleSetting(settings, "TT_SENSITIVITY", 1.0, 0.1, 3.0);

    const auto reverseCancellation = settings.find("TT_REVERSE_CANCELLATION");
    if (reverseCancellation == settings.end() || reverseCancellation->second == "1")
        turntableSettings.reverseCancellation = true;
    else if (reverseCancellation->second == "0")
        turntableSettings.reverseCancellation = false;
    else
    {
        LOG("[SETTINGS] Invalid value for TT_REVERSE_CANCELLATION; using default.");
        turntableSettings.reverseCancellation = true;
    }

    settings["TT_ACCELERATION"] = std::to_string(turntableSettings.acceleration);
    settings["TT_MAX_VELOCITY"] = std::to_string(turntableSettings.maximumVelocityPercent);
    settings["TT_COAST_DURATION_MS"] = std::to_string(turntableSettings.coastDurationMs);
    settings["TT_REVERSE_CANCELLATION"] = turntableSettings.reverseCancellation ? "1" : "0";
    settings["TT_SENSITIVITY"] = std::to_string(turntableSettings.sensitivity);
}

void SettingsManager::SaveConfigs()
{
    std::ofstream fout("config.ini");
    for(auto &pair : settings)
    {
        fout << pair.first << "=" << pair.second << "\n";
    }
}

void SettingsManager::SetRunAtStartup(bool enabled)
{
    #ifdef _WIN32
        if(enabled)
        {
            std::string progPath = GetFilePath();
            LONG status = RegSetKeyValueA(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "Mousedroid", REG_SZ, progPath.c_str(), (progPath.size() + 1) * sizeof(char));
        }
        else
            LONG status = RegDeleteKeyValueW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"Mousedroid");
    #endif 
}

void SettingsManager::SetMinimizeToTaskbar(bool enabled)
{
    settings["MINIMIZE_TASKBAR"] = std::to_string(enabled);
    SaveConfigs();
}

void SettingsManager::SetMoveSensitivity(int value)
{
    settings["MOVE_SENSITIVITY"] = std::to_string(value);
    InputManager::MOVE_SENSITIVITY = value;
    SaveConfigs();
}

void SettingsManager::SetScrollSensitivity(int value)
{
    settings["SCROLL_SENSITIVITY"] = std::to_string(value);
    InputManager::SCROLL_SENSITIVITY = value;
    SaveConfigs();
}

void SettingsManager::SetTurntableAcceleration(double value)
{
    turntableSettings.acceleration = std::clamp(value, 0.1, 5.0);
    settings["TT_ACCELERATION"] = std::to_string(turntableSettings.acceleration);
    SaveConfigs();
}

void SettingsManager::SetTurntableMaximumVelocityPercent(int value)
{
    turntableSettings.maximumVelocityPercent = std::clamp(value, 10, 200);
    settings["TT_MAX_VELOCITY"] = std::to_string(turntableSettings.maximumVelocityPercent);
    SaveConfigs();
}

void SettingsManager::SetTurntableCoastDurationMs(int value)
{
    turntableSettings.coastDurationMs = std::clamp(value, 100, 5000);
    settings["TT_COAST_DURATION_MS"] = std::to_string(turntableSettings.coastDurationMs);
    SaveConfigs();
}

void SettingsManager::SetTurntableReverseCancellation(bool enabled)
{
    turntableSettings.reverseCancellation = enabled;
    settings["TT_REVERSE_CANCELLATION"] = enabled ? "1" : "0";
    SaveConfigs();
}

void SettingsManager::SetTurntableSensitivity(double value)
{
    turntableSettings.sensitivity = std::clamp(value, 0.1, 3.0);
    settings["TT_SENSITIVITY"] = std::to_string(turntableSettings.sensitivity);
    SaveConfigs();
}

InputManager::TurntableSettings SettingsManager::GetTurntableSettings() const
{
    return turntableSettings;
}

std::string SettingsManager::GetFilePath()
{
    #ifdef _WIN32
        char buffer[128] = {0};
        GetModuleFileNameA(NULL, buffer, 128);
        return std::string(buffer);
    #endif
}

bool SettingsManager::GetRunAtStartup()
{
    return settings["RUN_STARTUP"] == "1";
}

bool SettingsManager::GetMinToTaskbar()
{
    return settings["MINIMIZE_TASKBAR"] == "1";
}

int SettingsManager::GetMoveSensitivity()
{
    return std::atoi(settings["MOVE_SENSITIVITY"].c_str());
}

int SettingsManager::GetScrollSensitivity()
{
    return std::atoi(settings["SCROLL_SENSITIVITY"].c_str());
}

bool SettingsManager::ADBOnOff()
{   
    if(bAdbStarted)
        ADBOff();
    else
        ADBOn();

    return bAdbStarted;
}

void SettingsManager::ADBOn()
{
    int a;
    #ifdef _WIN32
        //a = system("adb reverse tcp:6969 tcp:6969");        
        bAdbStarted = adb::start(6969);
    #else
        a = system("./adb reverse tcp:6969 tcp:6969");
        if(a == 0)
        {
            LOG("ADB Server [PORT 6969] STARTED");
            bAdbStarted = true;
        }
        else
            LOG("ADB Server FAILED");
    #endif
}

void SettingsManager::ADBOff()
{
    if(bAdbStarted)
    {
        #ifdef _WIN32
            //int a = system("adb reverse --remove-all");
            adb::kill(6969);
        #else
            int a = system("./adb reverse --remove-all");
            LOG("ADB Server [PORT 6969] STOPPED");
        #endif
        bAdbStarted = false;
    }
}

bool SettingsManager::ADBState()
{
    return bAdbStarted;
}