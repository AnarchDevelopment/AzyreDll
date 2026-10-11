#pragma once

#include <string>

#ifdef _MSC_VER
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>
#endif

namespace mc::window {

inline void setTitle(const std::string& title)
{
#ifdef _MSC_VER
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (...)
    {
    }

    try
    {
        auto action = winrt::Windows::ApplicationModel::Core::CoreApplication::MainView()
                          .CoreWindow()
                          .Dispatcher()
                          .RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                                    [title]() {
                                        winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()
                                            .Title(winrt::to_hstring(title));
                                    });
        (void)action;
    }
    catch (...)
    {
    }
#else
    // WinRT no esta disponible en toolchains no-MSVC (build de CI).
    (void)title;
#endif
}

}
