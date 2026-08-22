/*
Under an4rch Development Public Source License 1.0
*/

#include "WinRTTitle.hpp"

#include <string>
#include <thread>

// WinRT headers
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>

namespace {
    std::wstring s_title;
    HWND s_hwnd = nullptr;

    void ApplyViaWinRT() {
        try {
            winrt::init_apartment(winrt::apartment_type::single_threaded);

            // CoreApplication::GetCurrentView -> ApplicationView
            auto coreView = winrt::Windows::ApplicationModel::Core::CoreApplication::GetCurrentView();
            auto appView  = coreView.as<winrt::Windows::UI::ViewManagement::ApplicationView>();
            appView.Title(winrt::hstring(s_title.c_str()));
        } catch (...) {}

        try {
            // Fallback: CoreWindow::GetForCurrentThread
            auto window = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
            if (window) {
                auto appView = winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView();
                appView.Title(winrt::hstring(s_title.c_str()));
            }
        } catch (...) {}
    }

    void ApplyViaWin32() {
        if (s_hwnd && IsWindow(s_hwnd)) {
            SetWindowTextW(s_hwnd, s_title.c_str());
        }
    }

    void SetTitleThread(const wchar_t* title, HWND hwnd) {
        s_title = title;
        s_hwnd = hwnd;

        // 1. Try WinRT first (works natively on UWP)
        ApplyViaWinRT();

        // 2. Win32 fallback (covers cases where WinRT apartment is not available)
        ApplyViaWin32();

        // 3. Retry after a short delay (window may not be fully ready at injection time)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ApplyViaWinRT();
        ApplyViaWin32();

        // 4. Final retry after 2 seconds (some UWP windows reset their title briefly)
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        ApplyViaWinRT();
        ApplyViaWin32();
    }
}

void WinRTTitle::SetTitle(const wchar_t* title, HWND hwnd) {
    std::thread(SetTitleThread, title, hwnd).detach();
}
