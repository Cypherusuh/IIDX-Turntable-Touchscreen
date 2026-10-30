#pragma once

#ifdef _WIN32

#include "input/inputmanager.hpp"
#include "net/namedpipebridge.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace InputManager
{
    /**
     * @brief Class for handling input events on windows.
     */
   class Windows : public Base
    {
        public:
            Windows();
            ~Windows();

            bool StartBridge() const;
            void SetTurntableSettings(const TurntableSettings& settings) const;
            std::pair<double, double> GetTurntableVelocity() const;
            void StopMomentum() const;
            
        private:
            virtual void click() const override;
            virtual void right_click() const override;
            virtual void move(int dx, int dy) const override;
            virtual void scroll(int dy) const override;
            virtual void scroll_h(int dx) const override;
            virtual void down() const override;
            virtual void up() const override;
            virtual void send_key(uint8_t keycode, uint8_t modifier) const override;
            virtual void zoom(int scale) const override;
            virtual void media(uint8_t action) const override;

            struct AxisState
            {
                double velocity = 0.0;
                double fractionalDisplacement = 0.0;
            };

            void MovementLoop() const;
            void HandleBridgeStatus(bool connected, bool armed) const;
            void ResetMomentumLocked() const;
            void StopBridge() const;

            mutable std::mutex momentumMutex_;
            mutable std::condition_variable momentumCondition_;
            mutable std::deque<std::pair<int, int>> pendingMoves_;
            mutable TurntableSettings turntableSettings_;
            mutable AxisState xAxis_;
            mutable AxisState yAxis_;
            mutable bool bridgeConnected_ = false;
            mutable bool bridgeArmed_ = false;
            mutable bool stopping_ = false;
            mutable std::atomic<bool> stopStatusMonitor_{false};
            mutable std::thread movementThread_;
            mutable std::thread statusThread_;
            mutable Net::NamedPipeBridge pipe_;
            mutable PROCESS_INFORMATION bridgeProcess_{};
    };
}

#endif