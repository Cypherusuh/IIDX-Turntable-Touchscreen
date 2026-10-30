#include "wxmain.h"

#include "../res/icon.xpm"
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <wx/dcbuffer.h>
#endif

wxBEGIN_EVENT_TABLE(wxMain, wxFrame)
    EVT_ICONIZE(wxMain::HideWindow)
    EVT_CHECKBOX(CHK_STARTUP, wxMain::CheckboxCmdHandler)
    EVT_CHECKBOX(CHK_MINTASK, wxMain::CheckboxCmdHandler)
    EVT_COMMAND_SCROLL(SL_MOVE, wxMain::SliderCmdHandler)
    EVT_COMMAND_SCROLL(SL_SCROLL, wxMain::SliderCmdHandler)
#ifdef _WIN32
    EVT_TIMER(TT_SIMULATOR_TIMER, wxMain::UpdateTurntableSimulator)
#endif
wxEND_EVENT_TABLE()

wxMain::wxMain(SettingsManager &_settings, INPUT_MANAGER &_inputManager) :
    wxFrame(NULL, wxID_ANY, "Mousedroid", wxDefaultPosition, wxSize(APP_WIDTH, APP_HEIGHT), wxDEFAULT_FRAME_STYLE & ~wxRESIZE_BORDER & ~wxMAXIMIZE_BOX),
    settings(_settings),
    inputManager(_inputManager)
#ifdef _WIN32
    , turntableSimulatorTimer(this, TT_SIMULATOR_TIMER)
#endif
{
    wxnotebook = new wxNotebook(this, wxID_ANY, wxPoint(APP_MARGIN, APP_MARGIN), wxSize(APP_WIDTH - 23, APP_HEIGHT - 45), wxNB_TOP);

    tab_status = new wxPanel(wxnotebook, wxID_ANY);
    tab_log = new wxPanel(wxnotebook, wxID_ANY);
    tab_settings = new wxPanel(wxnotebook, wxID_ANY);
#ifdef _WIN32
    tab_turntable = new wxPanel(wxnotebook, wxID_ANY);
#endif

    wxnotebook->AddPage(tab_status, "Status", true);
    wxnotebook->AddPage(tab_settings, "Settings");
#ifdef _WIN32
    wxnotebook->AddPage(tab_turntable, "Turntable");
#endif
    wxnotebook->AddPage(tab_log, "Log");

    #ifdef _WIN32
        this->SetIcon(icon);
    #endif

    tb = new wxMTaskbar(this);
    tb->SetIcon(icon, "Mousedroid");
    
    InitStatusTab();
    InitSettingsTab();
#ifdef _WIN32
    InitTurntableTab();
    lastSimulatorUpdate = std::chrono::steady_clock::now();
    turntableSimulatorTimer.Start(40);
#endif
    InitLogTab();
}   

void wxMain::InitStatusTab()
{    
    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);

    // ==== This PC info ====
    wxStaticBoxSizer *ipsizer = new wxStaticBoxSizer(wxVERTICAL, tab_status, "This PC");

    wxBoxSizer *r1 = new wxBoxSizer(wxHORIZONTAL);

    r1->Add(new wxStaticText(tab_status, wxID_ANY, "Name: "));
    wxStaticText *sb1 = new wxStaticText(tab_status, HOST_NAME_TXT, "");
    sb1->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    r1->Add(sb1);

    wxBoxSizer *r2 = new wxBoxSizer(wxHORIZONTAL);
    r2->Add(new wxStaticText(tab_status, wxID_ANY, "IP: "));
    wxStaticText *sb2 = new wxStaticText(tab_status, HOST_IP_TXT, "");
    sb2->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    r2->Add(sb2);

    ipsizer->Add(r1);
    ipsizer->Add(r2);

    // ==== Device list ==== 
    wxdevlist = new wxDeviceList(tab_status, wxID_ANY);
        
    wxStaticBoxSizer *devsizer = new wxStaticBoxSizer(wxVERTICAL, tab_status, "Devices");
    devsizer->Add(wxdevlist, wxSizerFlags(1).Expand().Border(wxALL, 5));

    adbbutton = new wxButton(tab_status, wxID_ANY, "");
    adbbutton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &evt){
        bool res = settings.ADBOnOff();

        if(res)
            adbbutton->SetLabel("ADB On");
        else
            adbbutton->SetLabel("ADB Off");
    });

    devsizer->Add(adbbutton, wxSizerFlags().Right());


    topsizer->Add(ipsizer, wxSizerFlags().Expand().Border(wxALL, 5));
    topsizer->Add(devsizer, wxSizerFlags(1).Expand().Border(wxALL, 5));

    tab_status->SetSizerAndFit(topsizer);
}

void wxMain::InitLogTab()
{
    wxBoxSizer *topsizer3 = new wxBoxSizer(wxVERTICAL);
    
    wxListView *monitor = new wxListView(tab_log, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);

    monitor->AppendColumn("Time");
    monitor->AppendColumn("Message", wxLIST_FORMAT_LEFT, 380);

    Logger::monitor = monitor;

    topsizer3->Add(monitor, wxSizerFlags(1).Expand());

    tab_log->SetSizerAndFit(topsizer3);
}

void wxMain::InitSettingsTab()
{
    wxBoxSizer *topsizer2 = new wxBoxSizer(wxVERTICAL);

    // ==== System settings ====
    wxStaticBoxSizer *sysboxsizer = new wxStaticBoxSizer(wxVERTICAL, tab_settings, "System");

    #ifdef _WIN32
        // Startup checkbox
        wxCheckBox *chk_startup = new wxCheckBox(tab_settings, CHK_STARTUP, "Run at startup");
        chk_startup->SetValue(settings.GetRunAtStartup());
        sysboxsizer->Add(chk_startup, wxSizerFlags().Border(wxTOP, 10));
    #endif

    // Minimize to taskbar checkbox
    wxCheckBox *chk_mintask = new wxCheckBox(tab_settings, CHK_MINTASK, "Minimize to taskbar");
    chk_mintask->SetValue(settings.GetMinToTaskbar());
    sysboxsizer->Add(chk_mintask, wxSizerFlags().Border(wxTOP | wxBOTTOM, 5));

    // ==== Controls settings ====
    wxStaticBoxSizer *ctrlboxsizer = new wxStaticBoxSizer(wxVERTICAL, tab_settings, "Controls");

    // Move sensitivity slider
    wxBoxSizer *b1 = new wxBoxSizer(wxHORIZONTAL);
    b1->Add(new wxStaticText(tab_settings, wxID_ANY, "Move sensitivity:"), wxSizerFlags().Border(wxTOP, 4));

    wxSlider *ms = new wxSlider(
        tab_settings, SL_MOVE, 10, 1, 20, wxDefaultPosition, wxSize(150, wxDefaultSize.y), 
        #ifdef _WIN32
            wxSL_TICKS 
        #else 
            wxSL_VALUE_LABEL
        #endif
    );

    ms->SetValue(settings.GetMoveSensitivity());
    b1->Add(ms);
    ctrlboxsizer->Add(b1);

    // Scroll sensitivity slider
    wxBoxSizer *b2 = new wxBoxSizer(wxHORIZONTAL);
    b2->Add(new wxStaticText(tab_settings, wxID_ANY, "Scroll sensitivity:"), wxSizerFlags().Border(wxTOP, 4));

    wxSlider *ss = new wxSlider(
        tab_settings, SL_SCROLL, 10, 1, 20, wxDefaultPosition, wxSize(150, wxDefaultSize.y), 
        #ifdef _WIN32
            wxSL_TICKS 
        #else 
            wxSL_VALUE_LABEL
        #endif
    );
    
    ss->SetValue(settings.GetScrollSensitivity());
    b2->Add(ss);
    ctrlboxsizer->Add(b2);

    // ==== Info ====
    wxBoxSizer *info = new wxBoxSizer(wxVERTICAL);

    wxStaticText *t = new wxStaticText(tab_settings, wxID_ANY, "Mousedroid v1.5");
    t->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    info->Add(t, wxSizerFlags().Center());
    info->Add(new wxHyperlinkCtrl(tab_settings, wxID_ANY, "Github", "https://github.com/darusc/Mousedroid"), wxSizerFlags().Center());


    topsizer2->Add(sysboxsizer, wxSizerFlags().Expand().Border(wxALL, 5));
    topsizer2->Add(ctrlboxsizer, wxSizerFlags().Expand().Border(wxALL, 5));
    topsizer2->Add(info, wxSizerFlags().Expand().Border(wxTOP, 150));

    tab_settings->SetSizerAndFit(topsizer2);
}

void wxMain::SetHostInfo(std::string _Hostname, std::string _IpAddress)
{
    ((wxStaticText*)FindWindowById(HOST_NAME_TXT))->SetLabelText(_Hostname);
    ((wxStaticText*)FindWindowById(HOST_IP_TXT))->SetLabelText(_IpAddress);
}

void wxMain::HideWindow(wxIconizeEvent &evt)
{
    if(!settings.GetMinToTaskbar())
    {
        this->Hide();
        evt.Skip();
    }
}

void wxMain::CheckboxCmdHandler(wxCommandEvent &evt)
{
    if(evt.GetId() == CHK_STARTUP)
        settings.SetRunAtStartup(evt.IsChecked());
    else
        settings.SetMinimizeToTaskbar(evt.IsChecked());
}

void wxMain::SliderCmdHandler(wxScrollEvent &evt)
{
    if(evt.GetId() == SL_MOVE)
    {
        settings.SetMoveSensitivity(evt.GetPosition());
        return;
    }
    if (evt.GetId() == SL_SCROLL)
    {
        settings.SetScrollSensitivity(evt.GetPosition());
        return;
    }

#ifdef _WIN32
    if (evt.GetId() == SL_TT_ACCELERATION)
    {
        settings.SetTurntableAcceleration(evt.GetPosition() / 10.0);
        FindWindowById(LBL_TT_ACCELERATION)->SetLabel(
            wxString::Format("%.1fx", evt.GetPosition() / 10.0));
    }
    else if (evt.GetId() == SL_TT_MAX_VELOCITY)
    {
        settings.SetTurntableMaximumVelocityPercent(evt.GetPosition());
        FindWindowById(LBL_TT_MAX_VELOCITY)->SetLabel(
            wxString::Format("%d%%", evt.GetPosition()));
    }
    else if (evt.GetId() == SL_TT_COAST_DURATION)
    {
        settings.SetTurntableCoastDurationMs(evt.GetPosition());
        FindWindowById(LBL_TT_COAST_DURATION)->SetLabel(
            wxString::Format("%d ms", evt.GetPosition()));
    }
    else if (evt.GetId() == SL_TT_SENSITIVITY)
    {
        settings.SetTurntableSensitivity(evt.GetPosition() / 10.0);
        FindWindowById(LBL_TT_SENSITIVITY)->SetLabel(
            wxString::Format("%.1fx", evt.GetPosition() / 10.0));
    }
    else
    {
        return;
    }
    inputManager.SetTurntableSettings(settings.GetTurntableSettings());
#endif
}

#ifdef _WIN32
void wxMain::InitTurntableTab()
{
    const InputManager::TurntableSettings current = settings.GetTurntableSettings();
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    auto addSlider = [this, sizer](
        const char* label,
        int sliderId,
        int valueId,
        int value,
        int minimum,
        int maximum,
        const wxString& valueText,
        int lineSize)
    {
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(tab_turntable, wxID_ANY, label),
            wxSizerFlags(1).CenterVertical());
        auto *slider = new wxSlider(
            tab_turntable, sliderId, value, minimum, maximum, wxDefaultPosition,
            wxSize(220, wxDefaultSize.y), wxSL_TICKS);
        slider->SetLineSize(lineSize);
        row->Add(slider, wxSizerFlags().CenterVertical());
        row->Add(new wxStaticText(
            tab_turntable, valueId, valueText, wxDefaultPosition, wxSize(62, wxDefaultSize.y)),
            wxSizerFlags().Border(wxLEFT, 5).CenterVertical());
        sizer->Add(row, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, 5));
    };

    addSlider("Acceleration", SL_TT_ACCELERATION, LBL_TT_ACCELERATION,
        static_cast<int>(std::lround(current.acceleration * 10.0)), 1, 50,
        wxString::Format("%.1fx", current.acceleration), 1);
    addSlider("Maximum velocity", SL_TT_MAX_VELOCITY, LBL_TT_MAX_VELOCITY,
        current.maximumVelocityPercent, 10, 200,
        wxString::Format("%d%%", current.maximumVelocityPercent), 5);
    addSlider("Coast duration", SL_TT_COAST_DURATION, LBL_TT_COAST_DURATION,
        current.coastDurationMs, 100, 5000,
        wxString::Format("%d ms", current.coastDurationMs), 100);
    addSlider("Sensitivity", SL_TT_SENSITIVITY, LBL_TT_SENSITIVITY,
        static_cast<int>(std::lround(current.sensitivity * 10.0)), 1, 30,
        wxString::Format("%.1fx", current.sensitivity), 1);

    auto *reverse = new wxCheckBox(
        tab_turntable,
        CHK_TT_REVERSE,
        "Cancel existing velocity before accelerating in the opposite direction");
    reverse->SetValue(current.reverseCancellation);
    sizer->Add(reverse, wxSizerFlags().Border(wxALL, 6));

    turntableSimulatorStatus = new wxStaticText(
        tab_turntable, LBL_TT_SIMULATOR_STATUS, "X: 0 units/s    Y: 0 units/s");
    sizer->Add(turntableSimulatorStatus, wxSizerFlags().Border(wxLEFT | wxRIGHT, 6));
    sizer->Add(new wxStaticText(
        tab_turntable, wxID_ANY, "SpiceCFG selects which movement axis the game uses."),
        wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 5));

    turntableSimulator = new wxPanel(tab_turntable, wxID_ANY, wxDefaultPosition, wxSize(-1, 105));
    turntableSimulator->SetBackgroundStyle(wxBG_STYLE_PAINT);
    turntableSimulator->Bind(wxEVT_PAINT, &wxMain::PaintTurntableSimulator, this);
    sizer->Add(turntableSimulator, wxSizerFlags(1).Expand().Border(wxALL, 5));
    tab_turntable->SetSizerAndFit(sizer);

    reverse->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
        settings.SetTurntableReverseCancellation(event.IsChecked());
        inputManager.SetTurntableSettings(settings.GetTurntableSettings());
    });
}

void wxMain::UpdateTurntableSimulator(wxTimerEvent& evt)
{
    (void)evt;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - lastSimulatorUpdate).count();
    lastSimulatorUpdate = now;
    const auto velocity = inputManager.GetTurntableVelocity();
    const double activeVelocity =
        std::abs(velocity.first) >= std::abs(velocity.second) ? velocity.first : velocity.second;
    constexpr double TWO_PI = 6.28318530717958647692;
    simulatorAngle = std::fmod(simulatorAngle + activeVelocity * TWO_PI / 1000.0 * elapsed, TWO_PI);
    if (simulatorAngle < 0.0)
        simulatorAngle += TWO_PI;
    turntableSimulatorStatus->SetLabel(wxString::Format(
        "X: %.0f units/s    Y: %.0f units/s", velocity.first, velocity.second));
    turntableSimulator->Refresh(false);
}

void wxMain::PaintTurntableSimulator(wxPaintEvent& evt)
{
    (void)evt;
    wxAutoBufferedPaintDC dc(turntableSimulator);
    dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
    dc.Clear();
    const wxSize size = turntableSimulator->GetClientSize();
    const wxPoint center(size.x / 2, size.y / 2);
    const int radius = std::max(0, std::min(size.x, size.y) / 2 - 8);
    if (radius < 10)
        return;
    dc.SetPen(wxPen(wxColour(70, 70, 70), 2));
    dc.SetBrush(wxBrush(wxColour(48, 52, 60)));
    dc.DrawCircle(center, radius);
    dc.SetPen(wxPen(wxColour(100, 105, 115), 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawCircle(center, radius * 3 / 4);
    dc.DrawCircle(center, radius / 2);
    dc.DrawCircle(center, radius / 4);
    const int markerRadius = radius * 4 / 5;
    const wxPoint marker(
        center.x + static_cast<int>(std::cos(simulatorAngle) * markerRadius),
        center.y + static_cast<int>(std::sin(simulatorAngle) * markerRadius));
    dc.SetPen(wxPen(wxColour(70, 190, 255), 4));
    dc.DrawLine(center, marker);
}
#endif

void wxMain::UpdateUI()
{
    std::string label = (settings.ADBState()) ? "ADB On" : "ADB Off";
    adbbutton->SetLabel(label);
}

wxMain::~wxMain()
{
    delete tb;
}