#pragma once

#include <string>

namespace pitot::app {

// 开机自启：计划任务 \Pitot\Autostart，登录时以最高权限运行 Pitot.exe，不弹 UAC（D-002、D-045）。
// 状态只存在于任务本身，不在 config.ini 里写一份，免得两边对不上。
enum class AutostartState {
    Off,        // 没有任务，或任务被用户在任务计划程序里停用
    On,         // 任务存在、启用，且指向 exe_path
    OtherPath,  // 任务存在、启用，但指向别的 exe（挪过目录或换了构建）
    Unknown,    // 查不了（任务计划服务不可用等）
};

// registered_path 非空时，填入任务里登记的 exe 路径（Off / Unknown 时为空）
AutostartState QueryAutostart(const wchar_t* exe_path, std::wstring* registered_path = nullptr);

// enabled = true：创建或更新任务，指向 exe_path。enabled = false：删除任务。
// 需要管理员权限；失败返回 false，原因写日志。
bool SetAutostart(bool enabled, const wchar_t* exe_path);

}  // namespace pitot::app
