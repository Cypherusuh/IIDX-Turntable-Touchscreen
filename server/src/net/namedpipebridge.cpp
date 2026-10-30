#ifdef _WIN32

#include "net/namedpipebridge.h"

#include "TurntableDiagnostics.hpp"
#include "logger.h"
#include <windows.h>
#include <utility>

namespace Net
{
    NamedPipeBridge::NamedPipeBridge(std::string pipeName)
        : pipeName_(std::move(pipeName)),
          statusPipeName_(R"(\\.\pipe\MouseDroidHIDBridge.Status)")
    {
    }

    void NamedPipeBridge::WriteInt32LE(char* buffer, size_t index, std::int32_t value)
    {
        const auto raw = static_cast<std::uint32_t>(value);
        buffer[index] = static_cast<char>(raw & 0xFFu);
        buffer[index + 1] = static_cast<char>((raw >> 8) & 0xFFu);
        buffer[index + 2] = static_cast<char>((raw >> 16) & 0xFFu);
        buffer[index + 3] = static_cast<char>((raw >> 24) & 0xFFu);
    }

    bool NamedPipeBridge::SendMove(int dx, int dy) const
    {
        TurntableDiagnostics::Record(
            "TURN-PIPE",
            "write-attempt",
            "dx=" + std::to_string(dx) + " dy=" + std::to_string(dy));
        HANDLE pipe = CreateFileA(
            pipeName_.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            TurntableDiagnostics::Log(
                "TURN-PIPE",
                "write failed opening pipe dx=" + std::to_string(dx) + " dy=" + std::to_string(dy) +
                    " error=" + std::to_string(error));
            LOG("[PIPE] Failed to open HIDMaestro pipe: ", pipeName_, ". Windows error: ", error);
            return false;
        }

        char payload[8]{};
        WriteInt32LE(payload, 0, static_cast<std::int32_t>(dx));
        WriteInt32LE(payload, 4, static_cast<std::int32_t>(dy));
        DWORD bytesWritten = 0;
        const BOOL written = WriteFile(pipe, payload, sizeof(payload), &bytesWritten, nullptr);
        const DWORD error = written ? ERROR_SUCCESS : GetLastError();
        CloseHandle(pipe);
        if (!written || bytesWritten != sizeof(payload))
        {
            TurntableDiagnostics::Log(
                "TURN-PIPE",
                "write failed dx=" + std::to_string(dx) + " dy=" + std::to_string(dy) +
                    " bytes=" + std::to_string(bytesWritten) + " error=" + std::to_string(error));
            LOG("[PIPE] Failed to send relative mouse movement to HIDMaestro bridge.");
            return false;
        }

        TurntableDiagnostics::Record(
            "TURN-PIPE",
            "write-success",
            "dx=" + std::to_string(dx) + " dy=" + std::to_string(dy) +
                " bytes=" + std::to_string(bytesWritten));
        return true;
    }

    void NamedPipeBridge::MonitorStatus(
        const std::atomic<bool>& stopping,
        const std::function<void(bool, bool)>& onStatus) const
    {
        while (!stopping.load())
        {
            if (!WaitNamedPipeA(statusPipeName_.c_str(), 250))
            {
                Sleep(100);
                continue;
            }

            HANDLE pipe = CreateFileA(
                statusPipeName_.c_str(),
                GENERIC_READ,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
            {
                Sleep(100);
                continue;
            }

            HANDLE event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            if (event == nullptr)
            {
                TurntableDiagnostics::Log(
                    "TURN-STATUSPIPE",
                    "could not create status read event; error=" + std::to_string(GetLastError()));
                CloseHandle(pipe);
                continue;
            }

            TurntableDiagnostics::Log("TURN-STATUSPIPE", "connected");
            onStatus(true, false);
            while (!stopping.load())
            {
                char state = 0;
                OVERLAPPED operation{};
                operation.hEvent = event;
                ResetEvent(event);
                DWORD bytesRead = 0;
                BOOL completed = ReadFile(pipe, &state, 1, &bytesRead, &operation);
                if (!completed)
                {
                    if (GetLastError() != ERROR_IO_PENDING)
                        break;

                    DWORD waitResult = WAIT_TIMEOUT;
                    while (waitResult == WAIT_TIMEOUT && !stopping.load())
                        waitResult = WaitForSingleObject(event, 100);
                    if (stopping.load() && waitResult == WAIT_TIMEOUT)
                    {
                        CancelIoEx(pipe, &operation);
                        WaitForSingleObject(event, INFINITE);
                        break;
                    }
                    if (waitResult != WAIT_OBJECT_0 ||
                        !GetOverlappedResult(pipe, &operation, &bytesRead, FALSE))
                        break;
                }
                if (bytesRead != 1)
                    break;

                if (state == 'A')
                {
                    TurntableDiagnostics::Log("TURN-STATUSPIPE", "received ARMED");
                    onStatus(true, true);
                }
                else if (state == 'D')
                {
                    TurntableDiagnostics::Log("TURN-STATUSPIPE", "received DISARMED");
                    onStatus(true, false);
                }
                else if (state == 'H')
                {
                    TurntableDiagnostics::Record("TURN-STATUSPIPE", "heartbeat", "state=H");
                }
                else
                {
                    TurntableDiagnostics::Record(
                        "TURN-STATUSPIPE",
                        "unknown-status",
                        "byte=" + std::to_string(static_cast<unsigned char>(state)));
                }
            }

            CloseHandle(event);
            CloseHandle(pipe);
            TurntableDiagnostics::Log("TURN-STATUSPIPE", "disconnected");
            onStatus(false, false);
        }
        onStatus(false, false);
    }
}

#endif
