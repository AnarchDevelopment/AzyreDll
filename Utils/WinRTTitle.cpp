/*
Under an4rch Development Public Source License 1.0
*/

#include "WinRTTitle.hpp"
#include "../Assets/resource.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <winhttp.h>

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>

#pragma comment(lib, "winhttp.lib")

namespace {
    constexpr wchar_t kBaseWindowTitle[] = L"Azyre Client - 1.1.0";
    std::wstring s_title;
    HWND s_hwnd = nullptr;

    std::wstring BuildWindowTitle(const std::string& commit) {
        std::wstring wideCommit;
        for (const char character : commit) {
            wideCommit += static_cast<wchar_t>(static_cast<unsigned char>(character));
        }
        return L"[git-" + wideCommit + L"] " + kBaseWindowTitle;
    }

    std::string LoadGitHubToken() {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&LoadGitHubToken), &module)) {
            return {};
        }

        HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_GITHUB_INI), RT_RCDATA);
        HGLOBAL data = resource ? LoadResource(module, resource) : nullptr;
        const DWORD size = resource ? SizeofResource(module, resource) : 0;
        const char* content = data ? static_cast<const char*>(LockResource(data)) : nullptr;
        if (!content || size == 0) return {};

        const std::string ini(content, size);
        const size_t key = ini.find("token=");
        if (key == std::string::npos) return {};

        const size_t start = key + 6;
        const size_t end = ini.find_first_of("\r\n", start);
        std::string token = ini.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t first = token.find_first_not_of(" \t");
        const size_t last = token.find_last_not_of(" \t");
        if (first == std::string::npos) return {};
        return token.substr(first, last - first + 1);
    }

    std::string FetchGitHubResponse(const std::string& token) {
        HINTERNET session = WinHttpOpen(L"AzyreClient/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) return {};

        DWORD timeout = 5000;
        WinHttpSetOption(session, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(session, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(session, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        HINTERNET connection = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET",
            L"/repos/AnarchDevelopment/AzyreDll/commits?per_page=1", nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        std::string response;

        if (request) {
            std::wstring headers = L"Accept: application/vnd.github+json\r\nUser-Agent: AzyreClient\r\n";
            if (!token.empty()) {
                headers += L"Authorization: Bearer ";
                for (const char character : token) {
                    headers += static_cast<wchar_t>(static_cast<unsigned char>(character));
                }
                headers += L"\r\n";
            }
            WinHttpAddRequestHeaders(request, headers.c_str(),
                static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr)) {
                DWORD available = 0;
                while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
                    std::vector<char> chunk(available);
                    DWORD downloaded = 0;
                    if (!WinHttpReadData(request, chunk.data(), available, &downloaded) || downloaded == 0) break;
                    response.append(chunk.data(), downloaded);
                }
            }
            WinHttpCloseHandle(request);
        }
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    std::string ExtractCommit(const std::string& response) {
        const size_t key = response.find("\"sha\"");
        if (key == std::string::npos) return {};
        const size_t colon = response.find(':', key);
        const size_t start = colon == std::string::npos ? std::string::npos : response.find('"', colon + 1);
        const size_t end = start == std::string::npos ? std::string::npos : response.find('"', start + 1);
        if (end == std::string::npos || end - start - 1 < 7) return {};
        return response.substr(start + 1, 7);
    }

    std::string FetchLatestCommit() {
        const std::string tokenCommit = ExtractCommit(FetchGitHubResponse(LoadGitHubToken()));
        if (!tokenCommit.empty()) return tokenCommit;

        // The commits endpoint is public; retry without a token if the embedded
        // token is missing, expired, or rejected by GitHub.
        return ExtractCommit(FetchGitHubResponse({}));
    }

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

        const std::string latestCommit = FetchLatestCommit();
        if (!latestCommit.empty()) {
            s_title = BuildWindowTitle(latestCommit);
        }

        ApplyTitle();

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ApplyTitle();

        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        ApplyTitle();
    }
}

void WinRTTitle::SetTitle(HWND hwnd) {
    std::wstring titleCopy = BuildWindowTitle("unknown");
    s_title = titleCopy;
    s_hwnd = hwnd;
    ApplyViaWinRT();
    ApplyViaWin32();
    std::thread(SetTitleThread, std::move(titleCopy), hwnd).detach();
}
