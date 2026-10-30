#pragma once

#ifdef _WIN32

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace Net
{
    class NamedPipeBridge
    {
    public:
        explicit NamedPipeBridge(std::string pipeName = R"(\\.\pipe\MouseDroidHIDBridge)");

        bool SendMove(int dx, int dy) const;
        void MonitorStatus(
            const std::atomic<bool>& stopping,
            const std::function<void(bool, bool)>& onStatus) const;

    private:
        static void WriteInt32LE(char* buffer, size_t index, std::int32_t value);

        std::string pipeName_;
        std::string statusPipeName_;
    };
}

#endif
