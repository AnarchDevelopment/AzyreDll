/*
Under an4rch Development Public Source License 1.0
*/

#include "WinRTTitle.hpp"
#include "GitVersion.hpp"

#include <chrono>
#include <string>
#include <thread>

// WinRT headers
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>

namespace {
    std::wstring BuildWindowTitle() {
        std::wstring commit;
        for (const char character : std::string(AZYRE_GIT_COMMIT)) {
            commit += static_cast<wchar_t>(static_cast<unsigned char>(character));
        }
        return L"[git-" + commit + L"] Azyre Client - 1.0.9";
    }
    std::wstring s_title;
    HWND s_hwnd = nullptr;

    struct WindowSearch {
        DWORD processId;
        HWND hwnd;
        long long area;
    };

    struct TitleUpdate {
        DWORD processId;
        const wchar_t* title;
        bool updated;
    };

    BOOL CALLBACK FindMainWindow(HWND hwnd, LPARAM parameter) {
        auto* search = reinterpret_cast<WindowSearch*>(parameter);
        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);

        if (processId != search->processId || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) {
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

    BOOL CALLBACK UpdateProcessWindowTitle(HWND hwnd, LPARAM parameter) {
        auto* update = reinterpret_cast<TitleUpdate*>(parameter);
        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);

        if (processId == update->processId && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
            update->updated = SetWindowTextW(hwnd, update->title) != FALSE || update->updated;
        }

        return TRUE;
    }

    bool ApplyToProcessWindows() {
        TitleUpdate update{ GetCurrentProcessId(), s_title.c_str(), false };
        EnumWindows(UpdateProcessWindowTitle, reinterpret_cast<LPARAM>(&update));
        return update.updated;
    }

    HWND FindProcessWindow() {
        WindowSearch search{ GetCurrentProcessId(), nullptr, 0 };
        EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&search));
        return search.hwnd;
    }

    HWND GetRootWindow(HWND hwnd) {
        HWND root = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
        return root ? root : hwnd;
    }

    bool ApplyViaWinRT() {
        try {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
        } catch (const winrt::hresult_error& error) {
            if (error.code() != RPC_E_CHANGED_MODE) {
                return false;
            }
        } catch (...) {
            return false;
        }

        try {
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
        HWND discoveredWindow = FindProcessWindow();
        if (discoveredWindow) {
            s_hwnd = GetRootWindow(discoveredWindow);
        } else if (!s_hwnd || !IsWindow(s_hwnd)) {
            s_hwnd = nullptr;
        }

        if (!s_hwnd || !IsWindow(s_hwnd)) {
            return false;
        }

        s_hwnd = GetRootWindow(s_hwnd);
        const bool updated = SetWindowTextW(s_hwnd, s_title.c_str()) != FALSE;
        const bool updatedWindows = ApplyToProcessWindows();
        if (updated || updatedWindows) return true;

        s_hwnd = GetRootWindow(FindProcessWindow());
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

void WinRTTitle::SetTitle(HWND hwnd) {
    std::wstring titleCopy = BuildWindowTitle();
    s_title = titleCopy;
    s_hwnd = hwnd;
    ApplyViaWinRT();
    ApplyViaWin32();
    std::thread(SetTitleThread, std::move(titleCopy), hwnd).detach();
}
