cmake_minimum_required(VERSION 3.26)
project(Mousedroid)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

if(MSVC)
    add_compile_options(/Zc:preprocessor)
endif()

if(WIN32)
    if(MSVC)
        find_package(wxWidgets 3.2 REQUIRED CONFIG COMPONENTS net gl core base adv)
    else()
        find_package(wxWidgets REQUIRED net gl core base adv)
    endif()

    if(NOT ASIO_PATH)
        find_package(asio CONFIG REQUIRED)
    endif()
    
    # Define Windows-specific libraries (added user32 for SendInput)
    set(PLATFORM_LIBS ws2_32 wsock32 user32 shell32 advapi32)
    
    # MSVC vs MinGW resource handling
    if(MSVC)
        set(PLATFORM_SRC "src/input/win32/windowsinputmanager.cpp" resource.rc)
    else()
        set(PLATFORM_SRC "src/input/win32/windowsinputmanager.cpp" resource.o)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-subsystem,windows")
    endif()
else()
    # Linux: Use system-installed wxWidgets
    find_package(wxWidgets REQUIRED net gl core base adv)
    set(PLATFORM_SRC "src/input/linux/linuxinputmanager.cpp")
    set(PLATFORM_LIBS "")
    
    # Manual ASIO path for Linux
    set(ASIO_INCLUDE_DIR "${ASIO_PATH}/include") 
endif()

# Collect all sources but avoid double-including input managers
file(GLOB_RECURSE ALL_SRC src/*.cpp)

if(WIN32)
    set(PLATFORM_SRC "src/input/win32/windowsinputmanager.cpp")
else()
    set(PLATFORM_SRC "src/input/linux/linuxinputmanager.cpp")
    set(PLATFORM_LIBS "")
endif()

list(REMOVE_ITEM ALL_SRC ${PLATFORM_SRC})

add_executable(Mousedroid WIN32 ${ALL_SRC} ${PLATFORM_SRC})



if(TARGET wxWidgets::wxWidgets)
    target_link_libraries(Mousedroid PRIVATE wxWidgets::wxWidgets)
elseif(wxWidgets_FOUND)
    if(wxWidgets_USE_FILE)
        include(${wxWidgets_USE_FILE})
    endif()
    target_link_libraries(Mousedroid PRIVATE ${wxWidgets_LIBRARIES})
endif()

target_link_libraries(Mousedroid PRIVATE ${PLATFORM_LIBS})

if(WIN32)
    if(ASIO_PATH)
        if(NOT EXISTS "${ASIO_PATH}/include/asio.hpp")
            message(FATAL_ERROR "ASIO_PATH must contain include/asio.hpp")
        endif()
        target_include_directories(Mousedroid PRIVATE "${ASIO_PATH}/include")
        target_compile_definitions(Mousedroid PRIVATE ASIO_STANDALONE)
    else()
        target_link_libraries(Mousedroid PRIVATE asio::asio)
    endif()
else()
    target_include_directories(Mousedroid PRIVATE ./src ${ASIO_INCLUDE_DIR})
endif()

target_include_directories(Mousedroid PRIVATE ./src)

if(WIN32)
    target_compile_definitions(Mousedroid PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()