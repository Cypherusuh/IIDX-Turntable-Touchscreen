#ifdef _WIN32

#include "input/win32/windowsinputmanager.h"
#include "input/win32/keymap.h"
#include "TurntableDiagnostics.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace InputManager
{
    int MOVE_SENSITIVITY = 1;
    int SCROLL_SENSITIVITY = 1;

    namespace
    {
        constexpr double BASE_MAXIMUM_VELOCITY = 1000.0;
        constexpr double INPUT_ACCELERATION_RATE = 60.0;
        constexpr auto MOVEMENT_TICK = std::chrono::milliseconds(8);

        int ScaleMovement(int movement, double sensitivity)
        {
            if (movement == 0)
                return 0;
            const int scaled = static_cast<int>(std::lround(movement * sensitivity));
            return scaled == 0 ? (movement < 0 ? -1 : 1) : scaled;
        }

        template <typename Axis>
        int AdvanceAxis(Axis& axis, double elapsedSeconds, double deceleration)
        {
            if (axis.velocity == 0.0 || elapsedSeconds <= 0.0)
                return 0;

            const double direction = axis.velocity < 0.0 ? -1.0 : 1.0;
            const double movingTime = std::min(elapsedSeconds, std::abs(axis.velocity) / deceleration);
            const double distance = axis.velocity * movingTime -
                direction * 0.5 * deceleration * movingTime * movingTime;
            axis.velocity = direction *
                std::max(0.0, std::abs(axis.velocity) - deceleration * elapsedSeconds);
            axis.fractionalDisplacement += distance;

            const int wholeDisplacement = static_cast<int>(std::trunc(axis.fractionalDisplacement));
            axis.fractionalDisplacement -= wholeDisplacement;
            if (axis.velocity == 0.0)
                axis.fractionalDisplacement = 0.0;
            return wholeDisplacement;
        }

        template <typename Axis>
        void ApplyInput(
            Axis& axis,
            int movement,
            const TurntableSettings& settings,
            double maximumVelocity)
        {
            const double impulse = movement * settings.acceleration *
                settings.sensitivity * INPUT_ACCELERATION_RATE;
            if (settings.reverseCancellation && axis.velocity * impulse < 0.0)
            {
                const double remaining = std::abs(axis.velocity) - std::abs(impulse);
                axis.velocity = remaining > 0.0
                    ? std::copysign(remaining, axis.velocity)
                    : std::copysign(-remaining, impulse);
            }
            else if (!settings.reverseCancellation && axis.velocity * impulse < 0.0)
            {
                axis.velocity = impulse;
            }
            else
            {
                axis.velocity += impulse;
            }
            axis.velocity = std::max(-maximumVelocity, std::min(maximumVelocity, axis.velocity));
        }
    }

    Windows::Windows()
    {
        TurntableDiagnostics::Log("TURN-SERVER", "turntable diagnostics initialized");
        movementThread_ = std::thread(&Windows::MovementLoop, this);
        statusThread_ = std::thread([this]() {
            pipe_.MonitorStatus(stopStatusMonitor_, [this](bool connected, bool armed) {
                HandleBridgeStatus(connected, armed);
            });
        });
    }

    Windows::~Windows()
    {
        StopMomentum();
    }

    bool Windows::StartBridge() const
    {
        if (bridgeProcess_.hProcess != nullptr)
        {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(bridgeProcess_.hProcess, &exitCode) && exitCode == STILL_ACTIVE)
                return true;
            CloseHandle(bridgeProcess_.hProcess);
            bridgeProcess_ = {};
        }

        std::wstring executablePath(32768, L'\0');
        const DWORD length = GetModuleFileNameW(
            nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (length == 0 || length >= executablePath.size())
        {
            LOG("[BRIDGE] Could not resolve the MouseDroid executable path.");
            return false;
        }
        executablePath.resize(length);
        const std::filesystem::path applicationPath(executablePath);
        const auto bridgePath =
            applicationPath.parent_path() / L"HIDMaestroBridge" / L"MouseDroidHIDBridge.exe";
        std::error_code fileError;
        if (!std::filesystem::exists(bridgePath, fileError))
        {
            LOG("[BRIDGE] Bridge executable is missing: ", bridgePath.string());
            return false;
        }

        std::wstring commandLine = L"\"" + bridgePath.wstring() + L"\" --parent-pid " +
            std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        if (!CreateProcessW(
                bridgePath.c_str(),
                commandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                applicationPath.parent_path().c_str(),
                &startupInfo,
                &bridgeProcess_))
        {
            LOG("[BRIDGE] Could not start HIDMaestro bridge. Windows error: ", GetLastError());
            return false;
        }
        CloseHandle(bridgeProcess_.hThread);
        bridgeProcess_.hThread = nullptr;
        LOG("[BRIDGE] HIDMaestro bridge started.");
        return true;
    }

    void Windows::SetTurntableSettings(const TurntableSettings& settings) const
    {
        std::lock_guard<std::mutex> lock(momentumMutex_);
        turntableSettings_.acceleration = std::clamp(settings.acceleration, 0.1, 5.0);
        turntableSettings_.maximumVelocityPercent =
            std::clamp(settings.maximumVelocityPercent, 10, 200);
        turntableSettings_.coastDurationMs = std::clamp(settings.coastDurationMs, 100, 5000);
        turntableSettings_.reverseCancellation = settings.reverseCancellation;
        turntableSettings_.sensitivity = std::clamp(settings.sensitivity, 0.1, 3.0);
        const double maximumVelocity =
            BASE_MAXIMUM_VELOCITY * turntableSettings_.maximumVelocityPercent / 100.0;
        xAxis_.velocity = std::clamp(xAxis_.velocity, -maximumVelocity, maximumVelocity);
        yAxis_.velocity = std::clamp(yAxis_.velocity, -maximumVelocity, maximumVelocity);
        momentumCondition_.notify_all();
    }

    std::pair<double, double> Windows::GetTurntableVelocity() const
    {
        std::lock_guard<std::mutex> lock(momentumMutex_);
        return {xAxis_.velocity, yAxis_.velocity};
    }

    void Windows::ResetMomentumLocked() const
    {
        pendingMoves_.clear();
        xAxis_ = AxisState{};
        yAxis_ = AxisState{};
    }

    void Windows::HandleBridgeStatus(bool connected, bool armed) const
    {
        std::lock_guard<std::mutex> lock(momentumMutex_);
        const bool wasActive = bridgeConnected_ && bridgeArmed_;
        const bool isActive = connected && armed;
        if (bridgeConnected_ != connected || bridgeArmed_ != armed)
        {
            TurntableDiagnostics::Log(
                "TURN-STATUS",
                std::string("bridge state changed connected=") + (connected ? "true" : "false") +
                    " armed=" + (armed ? "true" : "false"));
        }
        if (!isActive || !wasActive)
        {
            const std::string reason = !connected
                ? "bridge-disconnected"
                : (!armed ? "bridge-disarmed" : "bridge-activated-reset");
            TurntableDiagnostics::Log(
                "TURN-RESET",
                reason + " queue=" + std::to_string(pendingMoves_.size()) +
                    " velocity_x=" + std::to_string(xAxis_.velocity) +
                    " velocity_y=" + std::to_string(yAxis_.velocity));
            ResetMomentumLocked();
        }
        bridgeConnected_ = connected;
        bridgeArmed_ = armed;
        momentumCondition_.notify_all();
    }

    void Windows::StopMomentum() const
    {
        {
            std::lock_guard<std::mutex> lock(momentumMutex_);
            if (!stopping_)
            {
                TurntableDiagnostics::Log(
                    "TURN-RESET",
                    "server-shutdown queue=" + std::to_string(pendingMoves_.size()) +
                        " velocity_x=" + std::to_string(xAxis_.velocity) +
                        " velocity_y=" + std::to_string(yAxis_.velocity));
                stopping_ = true;
                bridgeConnected_ = false;
                bridgeArmed_ = false;
                ResetMomentumLocked();
            }
        }
        momentumCondition_.notify_all();
        if (movementThread_.joinable())
            movementThread_.join();
        stopStatusMonitor_.store(true);
        if (statusThread_.joinable())
            statusThread_.join();
        StopBridge();
    }

    void Windows::StopBridge() const
    {
        if (bridgeProcess_.hProcess == nullptr)
            return;
        if (WaitForSingleObject(bridgeProcess_.hProcess, 1000) == WAIT_TIMEOUT)
        {
            TerminateProcess(bridgeProcess_.hProcess, 0);
            WaitForSingleObject(bridgeProcess_.hProcess, INFINITE);
        }
        CloseHandle(bridgeProcess_.hProcess);
        bridgeProcess_ = {};
    }

    void Windows::MovementLoop() const
    {
        using Clock = std::chrono::steady_clock;
        auto lastUpdate = Clock::now();
        auto nextTick = lastUpdate + MOVEMENT_TICK;
        for (;;)
        {
            std::vector<std::pair<int, int>> output;
            std::unique_lock<std::mutex> lock(momentumMutex_);
            momentumCondition_.wait_until(lock, nextTick, [this]() {
                return stopping_ || (bridgeConnected_ && bridgeArmed_ && !pendingMoves_.empty());
            });
            if (stopping_)
                return;
            if (!bridgeConnected_ || !bridgeArmed_)
            {
                lastUpdate = Clock::now();
                nextTick = lastUpdate + MOVEMENT_TICK;
                continue;
            }

            const auto now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - lastUpdate).count();
            lastUpdate = now;
            const double maximumVelocity =
                BASE_MAXIMUM_VELOCITY * turntableSettings_.maximumVelocityPercent / 100.0;
            const double deceleration =
                maximumVelocity / (turntableSettings_.coastDurationMs / 1000.0);
            const int coastX = AdvanceAxis(xAxis_, elapsed, deceleration);
            const int coastY = AdvanceAxis(yAxis_, elapsed, deceleration);
            if (coastX != 0 || coastY != 0)
            {
                output.emplace_back(coastX, coastY);
                TurntableDiagnostics::Record(
                    "TURN-WORKER",
                    "coast-output",
                    "dx=" + std::to_string(coastX) + " dy=" + std::to_string(coastY) +
                        " velocity_x=" + std::to_string(xAxis_.velocity) +
                        " velocity_y=" + std::to_string(yAxis_.velocity));
            }
            if (!pendingMoves_.empty())
            {
                const auto movement = pendingMoves_.front();
                pendingMoves_.pop_front();
                const int directX = ScaleMovement(movement.first, turntableSettings_.sensitivity);
                const int directY = ScaleMovement(movement.second, turntableSettings_.sensitivity);
                if (directX != 0 || directY != 0)
                {
                    output.emplace_back(directX, directY);
                    TurntableDiagnostics::Record(
                        "TURN-WORKER",
                        "direct-output",
                        "dx=" + std::to_string(directX) + " dy=" + std::to_string(directY));
                }
                ApplyInput(xAxis_, movement.first, turntableSettings_, maximumVelocity);
                ApplyInput(yAxis_, movement.second, turntableSettings_, maximumVelocity);
                TurntableDiagnostics::Record(
                    "TURN-WORKER",
                    "dequeued",
                    "input_dx=" + std::to_string(movement.first) +
                        " input_dy=" + std::to_string(movement.second) +
                        " queue=" + std::to_string(pendingMoves_.size()) +
                        " velocity_x=" + std::to_string(xAxis_.velocity) +
                        " velocity_y=" + std::to_string(yAxis_.velocity));
            }
            nextTick = now + MOVEMENT_TICK;
            lock.unlock();

            for (const auto& movement : output)
            {
                {
                    std::lock_guard<std::mutex> stateLock(momentumMutex_);
                    if (stopping_ || !bridgeConnected_ || !bridgeArmed_)
                        break;
                }
                if (!pipe_.SendMove(movement.first, movement.second))
                    LOG("[INPUT] HIDMaestro bridge unavailable. Relative movement was not forwarded.");
            }
        }
    }

    void Windows::click() const
    {
        INPUT input = {};
        ZeroMemory(&input, sizeof(input));
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;

        SendInput(1, &input, sizeof(INPUT));
    }

    void Windows::right_click() const
    {
        INPUT input;
        ZeroMemory(&input, sizeof(input));
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP;

        SendInput(1, &input, sizeof(INPUT));
    }

    void Windows::move(int dx, int dy) const
    {
        std::lock_guard<std::mutex> lock(momentumMutex_);
        if (stopping_)
        {
            TurntableDiagnostics::Record(
                "TURN-GATE",
                "rejected-server-stopping",
                "dx=" + std::to_string(dx) + " dy=" + std::to_string(dy));
            return;
        }
        if (!bridgeConnected_ || !bridgeArmed_)
        {
            TurntableDiagnostics::Record(
                "TURN-GATE",
                !bridgeConnected_ ? "rejected-bridge-disconnected" : "rejected-bridge-disarmed",
                "dx=" + std::to_string(dx) + " dy=" + std::to_string(dy) +
                    " connected=" + (bridgeConnected_ ? "true" : "false") +
                    " armed=" + (bridgeArmed_ ? "true" : "false"));
            return;
        }
        pendingMoves_.emplace_back(dx, dy);
        TurntableDiagnostics::Record(
            "TURN-GATE",
            "accepted",
            "dx=" + std::to_string(dx) + " dy=" + std::to_string(dy) +
                " queue=" + std::to_string(pendingMoves_.size()) +
                " connected=true armed=true");
        momentumCondition_.notify_one();
    }

    void Windows::scroll(int scroll_amount) const
    {
        INPUT input = {};
        ZeroMemory(&input, sizeof(input));
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = (int)(10 * (scroll_amount * (SCROLL_SENSITIVITY / 10.0)));

        SendInput(1, &input, sizeof(INPUT));
    }

    void Windows::scroll_h(int scroll_amount) const
    {
        INPUT inputs[3] = {};
        ZeroMemory(inputs, sizeof(inputs));
        
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_SHIFT;

        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dwFlags = MOUSEEVENTF_WHEEL;
        inputs[1].mi.mouseData = (int)(10 * (scroll_amount * (SCROLL_SENSITIVITY / 10.0)));

        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = VK_SHIFT;
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
    }

    void Windows::down() const
    {
        INPUT input = {};
        ZeroMemory(&input, sizeof(input));
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

        SendInput(1, &input, sizeof(INPUT));
    }

    void Windows::up() const
    {
        INPUT input = {};
        ZeroMemory(&input, sizeof(input));
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTUP;

        SendInput(1, &input, sizeof(INPUT));
    }

    void Windows::send_key(uint8_t keycode, uint8_t modifier) const
    {
        INPUT inputs[20];
        ZeroMemory(inputs, sizeof(inputs));
        int count = 0;

        // Extract modifier keys from the bitmask and add the to the input array
        for(const auto& mod : MODMAP)
        {
            if(modifier & mod.first)
            {
                inputs[count].type = INPUT_KEYBOARD;
                inputs[count].ki.wVk = mod.second;
                inputs[count].ki.dwFlags = 0;
                count++;
            }
        }

        auto it = KEYMAP.find(keycode);
        if(it != KEYMAP.end())
        {
            WORD vk = it->second;

            // Key Down
            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = vk;
            inputs[count].ki.dwFlags = 0;
            count++;

            // Key Up
            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = vk;
            inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
            count++;
        }

        // Release modifier keys in reverse order
        for (auto it_mod = MODMAP.rbegin(); it_mod != MODMAP.rend(); ++it_mod) 
        {
            if (modifier & it_mod->first) 
            {
                inputs[count].type = INPUT_KEYBOARD;
                inputs[count].ki.wVk = it_mod->second;
                inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
                count++;
            }
        }

        if(count > 0) {
            SendInput(count, inputs, sizeof(INPUT));
        }
    }

    void Windows::zoom(int scale) const
    {
        INPUT inputs[3] = {};
        ZeroMemory(inputs, sizeof(inputs));
        
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_CONTROL;

        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dwFlags = MOUSEEVENTF_WHEEL;
        inputs[1].mi.mouseData = scale;

        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wVk = VK_CONTROL;
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
    }

    void Windows::media(uint8_t action) const
    {
        INPUT inputs[2] = {};
		ZeroMemory(inputs, sizeof(inputs));

		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = MEDIAMAP.at(action);

		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = MEDIAMAP.at(action);
		inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

		SendInput(2, inputs, sizeof(INPUT));
    }
}

#endif