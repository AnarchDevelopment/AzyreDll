#pragma once

#include <string>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace mc::window {

inline void setTitle(const std::string& title)
{
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
}

}
