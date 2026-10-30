#include "TurntableDiagnostics.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <iostream>
#endif

namespace
{
    struct Aggregate
    {
        std::string component;
        std::string event;
        std::string details;
        std::size_t count = 0;
    };

    std::string Timestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
#ifdef _WIN32
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif
        std::ostringstream result;
        result << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << '.';
        result << std::setw(3) << std::setfill('0') << milliseconds;
        return result.str();
    }

    bool IsVerbose()
    {
        const char* value = std::getenv("MOUSEDROID_TURNTABLE_VERBOSE");
        return value != nullptr && (std::string(value) == "1" || std::string(value) == "true");
    }

    std::filesystem::path GetLogPath()
    {
#ifdef _WIN32
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length > 0 && length < path.size())
        {
            path.resize(length);
            return std::filesystem::path(path).parent_path() / L"turntable-diagnostics.log";
        }
#endif
        return "turntable-diagnostics.log";
    }

    class Writer
    {
    public:
        Writer()
            : verbose_(IsVerbose()),
              output_(GetLogPath(), std::ios::out | std::ios::app),
              worker_(&Writer::Run, this)
        {
            Log("TURN-DIAG", std::string("diagnostics started; verbose=") + (verbose_ ? "on" : "off"));
        }

        ~Writer()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
            }
            condition_.notify_one();
            if (worker_.joinable())
                worker_.join();
        }

        void Log(const std::string& component, const std::string& message)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_.push_back(Format(component, message));
            }
            condition_.notify_one();
        }

        void Record(const std::string& component, const std::string& event, const std::string& details)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (verbose_)
            {
                pending_.push_back(Format(component, event + " " + details));
                condition_.notify_one();
                return;
            }
            auto& aggregate = aggregates_[component + '\x1f' + event];
            aggregate.component = component;
            aggregate.event = event;
            aggregate.details = details;
            ++aggregate.count;
        }

    private:
        static std::string Format(const std::string& component, const std::string& message)
        {
            return "[" + Timestamp() + "] [" + component + "] " + message;
        }

        void WriteLine(const std::string& line)
        {
            if (output_.is_open())
            {
                output_ << line << '\n';
                output_.flush();
                if (!output_)
                {
#ifdef _WIN32
                    OutputDebugStringA("Turntable diagnostics could not write its log file.\n");
#else
                    std::cerr << "Turntable diagnostics could not write its log file.\n";
#endif
                }
                return;
            }
#ifdef _WIN32
            OutputDebugStringA((line + "\n").c_str());
#else
            std::cerr << line << '\n';
#endif
        }

        void Run()
        {
            using Clock = std::chrono::steady_clock;
            auto nextFlush = Clock::now() + std::chrono::milliseconds(400);
            for (;;)
            {
                std::vector<std::string> lines;
                bool stopping = false;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    condition_.wait_until(lock, nextFlush, [this]() {
                        return stopping_ || !pending_.empty();
                    });
                    while (!pending_.empty())
                    {
                        lines.push_back(std::move(pending_.front()));
                        pending_.pop_front();
                    }
                    const auto now = Clock::now();
                    if (now >= nextFlush || stopping_)
                    {
                        for (const auto& entry : aggregates_)
                        {
                            lines.push_back(Format(
                                "TURN-DIAG",
                                "[" + entry.second.component + "] " + entry.second.event +
                                    " count=" + std::to_string(entry.second.count) +
                                    " last={" + entry.second.details + "}"));
                        }
                        aggregates_.clear();
                        nextFlush = now + std::chrono::milliseconds(400);
                    }
                    stopping = stopping_;
                }
                for (const auto& line : lines)
                    WriteLine(line);
                if (stopping)
                    return;
            }
        }

        const bool verbose_;
        std::ofstream output_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<std::string> pending_;
        std::map<std::string, Aggregate> aggregates_;
        bool stopping_ = false;
        std::thread worker_;
    };

    Writer& GetWriter()
    {
        static Writer writer;
        return writer;
    }
}

namespace TurntableDiagnostics
{
    void Log(const std::string& component, const std::string& message)
    {
        GetWriter().Log(component, message);
    }

    void Record(const std::string& component, const std::string& event, const std::string& details)
    {
        GetWriter().Record(component, event, details);
    }
}
