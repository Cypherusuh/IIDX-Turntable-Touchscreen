#include "wxapplication.h"

wxIMPLEMENT_APP(wxApplication);

#include "../res/icon.xpm"
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <filesystem>

namespace
{
    bool IsProcessElevated()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        TOKEN_ELEVATION elevation{};
        DWORD returnedSize = 0;
        const BOOL succeeded = GetTokenInformation(
            token, TokenElevation, &elevation, sizeof(elevation), &returnedSize);
        CloseHandle(token);
        return succeeded && elevation.TokenIsElevated != 0;
    }

    bool RelaunchElevated()
    {
        std::wstring executablePath(32768, L'\0');
        const DWORD length = GetModuleFileNameW(
            nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (length == 0 || length >= executablePath.size())
        {
            MessageBoxW(nullptr, L"Could not resolve the MouseDroid executable path.",
                L"MouseDroid", MB_OK | MB_ICONERROR);
            return false;
        }
        executablePath.resize(length);
        const std::filesystem::path applicationPath(executablePath);
        const std::wstring workingDirectory = applicationPath.parent_path().wstring();

        SHELLEXECUTEINFOW launch{};
        launch.cbSize = sizeof(launch);
        launch.fMask = SEE_MASK_NOCLOSEPROCESS;
        launch.lpVerb = L"runas";
        launch.lpFile = executablePath.c_str();
        launch.lpDirectory = workingDirectory.c_str();
        launch.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&launch))
        {
            if (GetLastError() != ERROR_CANCELLED)
            {
                MessageBoxW(nullptr, L"Could not restart MouseDroid with administrator permission.",
                    L"MouseDroid", MB_OK | MB_ICONERROR);
            }
            return false;
        }
        if (launch.hProcess != nullptr)
            CloseHandle(launch.hProcess);
        return true;
    }
}
#endif

wxApplication::wxApplication() { }

bool wxApplication::OnInit()
{
#ifdef _WIN32
    if (!IsProcessElevated())
    {
        RelaunchElevated();
        return false;
    }
    if (!inputManager.StartBridge())
    {
        wxMessageBox(
            "The HIDMaestro bridge could not be started. Confirm that HIDMaestroBridge\\MouseDroidHIDBridge.exe is beside MouseDroid.",
            "MouseDroid bridge error",
            wxOK | wxICON_ERROR);
        return false;
    }
    inputManager.SetTurntableSettings(settings.GetTurntableSettings());
#endif

    main_frame = new wxMain(settings, inputManager);
    
    server = new Server(6969, *this, settings, inputManager);

    Server::HostInfo hostInfo = server->GetHostInfo();
    
    main_frame->Bind(wxEVT_CLOSE_WINDOW, &wxApplication::OnWindowCloseEvent, this);
    main_frame->SetHostInfo(std::get<0>(hostInfo), std::get<1>(hostInfo));

    if(!settings.GetRunAtStartup())
        main_frame->Show();
    
    server->Start();

    main_frame->UpdateUI();

    return true;
}

int wxApplication::OnExit()
{
#ifdef _WIN32
    inputManager.StopMomentum();
#endif
    return wxApp::OnExit();
}

void wxApplication::OnDeviceConnected(std::string device) const
{
    auto devices = server->GetConnectedDevicesInfo();
    main_frame->wxdevlist->SetDevices(devices);
}

void wxApplication::OnDeviceDisconnected(std::string device) const
{
	auto devices = server->GetConnectedDevicesInfo();
    main_frame->wxdevlist->SetDevices(devices);
}

void wxApplication::OnWindowCloseEvent(wxCloseEvent &evt)
{
    wxMessageDialog *box = new wxMessageDialog(main_frame, "This will disconnect all connected devices. Proceed?", "Confirm", wxYES_NO | wxICON_INFORMATION);

    int res = box->ShowModal();

    if(res == wxID_YES)
    {
        main_frame->Hide();
#ifdef _WIN32
        inputManager.StopMomentum();
#endif
        server->Close();
        Logger::monitor->Destroy();
        main_frame->Destroy();
    }
}
