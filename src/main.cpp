#include "viewer_window.hpp"

#include "../resources/resource.h"

#include <commctrl.h>
#include <d2d1.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<std::wstring> CommandLineArguments() {
    int count = 0;
    LPWSTR* rawArguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> arguments;
    if (rawArguments) {
        arguments.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            arguments.emplace_back(rawArguments[index]);
        }
        LocalFree(rawArguments);
    }
    return arguments;
}

int RunSelfTest() {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        return 10;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    const HRESULT wicResult = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory));
    if (FAILED(wicResult)) {
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        return 11;
    }

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    D2D1_FACTORY_OPTIONS options{};
    const HRESULT d2dResult = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        &options,
        reinterpret_cast<void**>(d2dFactory.GetAddressOf()));
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return SUCCEEDED(d2dResult) ? 0 : 12;
}

HWND FindExistingWindow() {
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (HWND window = FindWindowW(pureview::kWindowClassName, nullptr)) {
            return window;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return nullptr;
}

void ForwardToExistingInstance(HWND window, const std::wstring& path) {
    if (!path.empty()) {
        COPYDATASTRUCT data{};
        data.dwData = 1;
        data.cbData = static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t));
        data.lpData = const_cast<wchar_t*>(path.c_str());
        SendMessageTimeoutW(
            window,
            WM_COPYDATA,
            0,
            reinterpret_cast<LPARAM>(&data),
            SMTO_ABORTIFHUNG,
            1500,
            nullptr);
    }
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetCurrentProcessExplicitAppUserModelID(L"PureView.Native");

    const auto arguments = CommandLineArguments();
    if (arguments.size() >= 2 && arguments[1] == L"--self-test") {
        return RunSelfTest();
    }
    if (arguments.size() >= 2 && arguments[1] == L"--open-default-apps") {
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
            nullptr,
            L"open",
            L"ms-settings:defaultapps?registeredAppUser=PureView",
            nullptr,
            nullptr,
            SW_SHOWNORMAL));
        return result > 32 ? 0 : 20;
    }

    const std::wstring initialPath = arguments.size() >= 2
        ? arguments[1]
        : std::wstring{};
    HANDLE instanceMutex = CreateMutexW(
        nullptr,
        TRUE,
        L"Local\\PureView.Native.SingleInstance");
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindExistingWindow()) {
            ForwardToExistingInstance(existing, initialPath);
            CloseHandle(instanceMutex);
            return 0;
        }
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        if (instanceMutex) CloseHandle(instanceMutex);
        return 2;
    }

    INITCOMMONCONTROLSEX controls{
        sizeof(controls),
        ICC_STANDARD_CLASSES,
    };
    InitCommonControlsEx(&controls);

    pureview::ViewerWindow viewer;
    if (!viewer.Create(instance)) {
        if (SUCCEEDED(comResult)) CoUninitialize();
        if (instanceMutex) CloseHandle(instanceMutex);
        return 3;
    }
    viewer.Show(showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand);
    if (!initialPath.empty()) {
        viewer.OpenPath(initialPath);
    } else {
        viewer.PromptToOpen();
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    if (instanceMutex) {
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
    }
    return static_cast<int>(message.wParam);
}
