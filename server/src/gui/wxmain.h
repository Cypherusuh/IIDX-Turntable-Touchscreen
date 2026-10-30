#pragma once

#define APP_WIDTH 500
#define APP_HEIGHT 450

#define APP_MARGIN 10

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/msgdlg.h>
#include <wx/hyperlink.h>
#include <wx/timer.h>
#include <chrono>
#include <utility>

#include "gui/wxdevicelist.h"
#include "settingsmanager.h"
#include "logger.h"
#include "gui/taskbar.h"
#include "input/input.h"

#define CHK_STARTUP 1001
#define CHK_MINTASK 1002
#define SL_MOVE 1003
#define SL_SCROLL 1004
#define CHK_TT_REVERSE 1020
#define SL_TT_ACCELERATION 1021
#define SL_TT_MAX_VELOCITY 1022
#define SL_TT_COAST_DURATION 1023
#define SL_TT_SENSITIVITY 1024
#define LBL_TT_ACCELERATION 1031
#define LBL_TT_MAX_VELOCITY 1032
#define LBL_TT_COAST_DURATION 1033
#define LBL_TT_SENSITIVITY 1034
#define LBL_TT_SIMULATOR_STATUS 1035
#define TT_SIMULATOR_TIMER 1040

#define HOST_NAME_TXT 1010
#define HOST_IP_TXT 1011

class wxMain: public wxFrame
{
    public:
        wxDeviceList *wxdevlist = nullptr;
        wxPanel *tab_log = nullptr;
        wxButton *adbbutton = nullptr;

        SettingsManager &settings;
        INPUT_MANAGER &inputManager;

        wxMain(SettingsManager &_settings, INPUT_MANAGER &_inputManager);
        ~wxMain();
    
        void SetHostInfo(std::string _Hostname, std::string _IpAddress);

        void UpdateUI();
        
    private:
        wxPanel *tab_status = nullptr;
        wxPanel *tab_settings = nullptr;
#ifdef _WIN32
        wxPanel *tab_turntable = nullptr;
        wxPanel *turntableSimulator = nullptr;
        wxStaticText *turntableSimulatorStatus = nullptr;
        wxTimer turntableSimulatorTimer;
        std::chrono::steady_clock::time_point lastSimulatorUpdate;
        double simulatorAngle = 0.0;
#endif
        wxNotebook *wxnotebook = nullptr;
        wxMTaskbar *tb = nullptr;
        
        wxDECLARE_EVENT_TABLE();    

        void InitStatusTab();   
        void InitLogTab();
        void InitSettingsTab();
#ifdef _WIN32
        void InitTurntableTab();
        void UpdateTurntableSimulator(wxTimerEvent &evt);
        void PaintTurntableSimulator(wxPaintEvent &evt);
#endif

        void CheckboxCmdHandler(wxCommandEvent &evt);
        void SliderCmdHandler(wxScrollEvent &evt);

        void HideWindow(wxIconizeEvent &evt);
        void CloseWindow(wxCloseEvent &evt);
};
