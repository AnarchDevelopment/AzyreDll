/*
Under an4rch Development Public Source License 1.0
*/

#include "WinRTTitle.hpp"

#include <chrono>
#include <cwchar>
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

    struct WindowSearch {
        DWORD processId;
        bool requireMinecraftTitle;
        HWND hwnd;
        long long area;
    };

    BOOL CALLBACK FindMainWindow(HWND hwnd, LPARAM parameter) {
        auto* search = reinterpret_cast<WindowSearch*>(parameter);
        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);

        if (processId != search->processId || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) {
            return TRUE;
        }

        const int titleLength = GetWindowTextLengthW(hwnd);
        if (titleLength <= 0) {
            return TRUE;
        }

        std::wstring windowTitle(static_cast<size_t>(titleLength) + 1, L'\0');
        GetWindowTextW(hwnd, windowTitle.data(), titleLength + 1);
        windowTitle.resize(std::wcslen(windowTitle.c_str()));

        constexpr wchar_t minecraftPrefix[] = L"Minecraft:";
        constexpr size_t minecraftPrefixLength = sizeof(minecraftPrefix) / sizeof(wchar_t) - 1;
        if (search->requireMinecraftTitle &&
            (windowTitle.size() < minecraftPrefixLength ||
             _wcsnicmp(windowTitle.c_str(), minecraftPrefix, minecraftPrefixLength) != 0)) {
            return TRUE;
        }

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect)) {
            return TRUE;
        }

        const long long width = std::max<LONG>(0, rect.right - rect.left);
        const long long height = std::max<LONG>(0, rect.bottom - rect.top);
        const long long area = width * height;
        if (!search->hwnd || area > search->area) {
            search->hwnd = hwnd;
            search->area = area;
        }

        return TRUE;
    }

    HWND FindProcessWindow() {
        WindowSearch search{ GetCurrentProcessId(), true, nullptr, 0 };
        EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&search));

        if (!search.hwnd) {
            search.requireMinecraftTitle = false;
            EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&search));
        }

        return search.hwnd;
    }

    bool ApplyViaWinRT() {
        try {
            winrt::init_apartment(winrt::apartment_type::single_threaded);

            auto coreView = winrt::Windows::ApplicationModel::Core::CoreApplication::GetCurrentView();
            auto appView  = coreView.as<winrt::Windows::UI::ViewManagement::ApplicationView>();
            appView.Title(winrt::hstring(s_title.c_str()));
            return true;
        } catch (...) {
        }

        try {
            auto window = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
            if (window) {
                auto appView = winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView();
                appView.Title(winrt::hstring(s_title.c_str()));
                return true;
            }
        } catch (...) {
        }

        return false;
    }

    bool ApplyViaWin32() {
        if (!s_hwnd || !IsWindow(s_hwnd)) {
            s_hwnd = FindProcessWindow();
        }

        if (!s_hwnd || !IsWindow(s_hwnd)) {
            return false;
        }

        if (SetWindowTextW(s_hwnd, s_title.c_str()) != FALSE) {
            return true;
        }

        s_hwnd = FindProcessWindow();
        return s_hwnd && SetWindowTextW(s_hwnd, s_title.c_str()) != FALSE;
    }

    void ApplyTitle() {
        const bool winrtApplied = ApplyViaWinRT();

        // WinRT can report success from an injected thread while targeting a
        // different view. Update the real game HWND when one is available.
        if (!winrtApplied || (s_hwnd && IsWindow(s_hwnd))) {
            ApplyViaWin32();
        }
    }

    void SetTitleThread(std::wstring title, HWND hwnd) {
        s_title = std::move(title);
        s_hwnd = hwnd;

        ApplyTitle();

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ApplyTitle();

        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        ApplyTitle();
    }
}

void WinRTTitle::SetTitle(const wchar_t* title, HWND hwnd) {
    std::thread(SetTitleThread, title ? std::wstring(title) : std::wstring(), hwnd).detach();
}
