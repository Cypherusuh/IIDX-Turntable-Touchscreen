#pragma once

#include <string>
#include <iostream>
#include <vector>
#include <sstream>

#ifdef __unix__
    #include <climits>
#endif

#include "logger.h"
#include "TurntableDiagnostics.hpp"

namespace InputManager
{
    struct TurntableSettings
    {
        double acceleration = 1.0;
        int maximumVelocityPercent = 100;
        int coastDurationMs = 1000;
        bool reverseCancellation = true;
        double sensitivity = 1.0;
    };

    struct Command 
    {
        char type;
        std::vector<double> args;
    };   

    const char LCLICK = 0x01;
    const char RCLICK = 0x02;
    const char DOWN = 0x03;
    const char UP = 0x04;
    const char MOVE = 0x05;
    const char SCROLL = 0x06;
    const char KEYPRESS = 0x07;
    const char SCROLL_H = 0x08;
    const char ZOOM = 0x09;
    const char MEDIA = 0x0A;

    extern int MOVE_SENSITIVITY;
    extern int SCROLL_SENSITIVITY;
    
    /**
     * @brief Base class to handle all input events.
     * Specialized os classes are dervived from this.
     */
    class Base
    {
        public:
            void execute(const char* bytes, size_t size, const char* transport = "unknown") const
            {
                if (bytes == nullptr || size == 0)
                    return;

                size_t index = 0;
                while (index < size)
                {
                    const unsigned char command = static_cast<unsigned char>(bytes[index]);
                    switch(command)
                    {
                    case InputManager::MOVE:
                        if (index + 2 >= size)
                        {
                            TurntableDiagnostics::Record(
                                "TURN-PACKET", "incomplete-command",
                                std::string("transport=") + transport + " type=MOVE remaining=" +
                                    std::to_string(size - index));
                            return;
                        }
                        {
                            const int dx = -static_cast<int>(static_cast<signed char>(bytes[index + 1]));
                            const int dy = -static_cast<int>(static_cast<signed char>(bytes[index + 2]));
                            TurntableDiagnostics::Record(
                                "TURN-PACKET",
                                dx == 0 && dy == 0 ? "MOVE-zero" : "MOVE-nonzero",
                                std::string("transport=") + transport +
                                    " decoded_dx=" + std::to_string(dx) +
                                    " decoded_dy=" + std::to_string(dy));
                            move(dx, dy);
                        }
                        index += 3;
                        break;
                    
                    case InputManager::LCLICK:
                        click();
                        ++index;
                        break;

                    case InputManager::RCLICK:
                        right_click();
                        ++index;
                        break;

                    case InputManager::SCROLL:
                        if (index + 1 >= size)
                        {
                            TurntableDiagnostics::Record(
                                "TURN-PACKET", "incomplete-command",
                                std::string("transport=") + transport + " type=SCROLL remaining=" +
                                    std::to_string(size - index));
                            return;
                        }
                        scroll(bytes[index + 1]);
                        index += 2;
                        break;

                    case InputManager::SCROLL_H:
                        if (index + 1 >= size)
                        {
                            TurntableDiagnostics::Record(
                                "TURN-PACKET", "incomplete-command",
                                std::string("transport=") + transport + " type=SCROLL_H remaining=" +
                                    std::to_string(size - index));
                            return;
                        }
                        scroll_h(bytes[index + 1]);
                        index += 2;
                        break;

                    case InputManager::DOWN:
                        down();
                        ++index;
                        break;

                    case InputManager::UP:
                        up();
                        ++index;
                        break;

                    case InputManager::KEYPRESS:
                        if (index + 2 >= size)
                        {
                            TurntableDiagnostics::Record(
                                "TURN-PACKET", "incomplete-command",
                                std::string("transport=") + transport + " type=KEYPRESS remaining=" +
                                    std::to_string(size - index));
                            return;
                        }
                        send_key(
                            static_cast<uint8_t>(bytes[index + 1]),
                            static_cast<uint8_t>(bytes[index + 2]));
                        index += 3;
                        break;

                    case InputManager::MEDIA:
                        if (index + 1 >= size)
                        {
                            TurntableDiagnostics::Record(
                                "TURN-PACKET", "incomplete-command",
                                std::string("transport=") + transport + " type=MEDIA remaining=" +
                                    std::to_string(size - index));
                            return;
                        }
                        media(static_cast<uint8_t>(bytes[index + 1]));
                        index += 2;
                        break;

                    case InputManager::ZOOM:
                        if (index + 1 >= size)
                        {
                            TurntableDiagnostics::Record(
                                "TURN-PACKET", "incomplete-command",
                                std::string("transport=") + transport + " type=ZOOM remaining=" +
                                    std::to_string(size - index));
                            return;
                        }
                        zoom(bytes[index + 1]);
                        index += 2;
                        break;

                    default:
                        ++index;
                        break;
                    }
                }
            }
            
        private:
            virtual void click() const = 0;
            virtual void right_click() const = 0;
            virtual void move(int dx, int dy) const = 0;
            virtual void scroll(int dy) const = 0;
            virtual void scroll_h(int dx) const = 0;
            virtual void down() const = 0;
            virtual void up() const  = 0;
            virtual void send_key(uint8_t keycode, uint8_t modifier) const = 0;
            virtual void zoom(int scale) const = 0;
            virtual void media(uint8_t action) const = 0;
    };
}