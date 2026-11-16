#pragma once

#include <windows.h>

// 高温通知（M8，D-079）。Pitot 平时没有通知区图标；要通知时临时加一个，用它弹出系统通知，
// 过一会儿再删掉。点通知会打开浮窗的传感器面板。

namespace pitot::app {

class HotNotice {
public:
    // owner 收通知区回调消息 callback_message
    void Show(HWND owner, UINT callback_message, const wchar_t* title, const wchar_t* body);
    void Remove();
    [[nodiscard]] bool shown() const noexcept { return shown_; }

private:
    HWND owner_ = nullptr;
    bool shown_ = false;
};

}  // namespace pitot::app
