#pragma once

// 固定名称集中在这里。改动会破坏自启、单实例、崩溃清理或与 FanControl 的共存，见 AGENTS.md "Fixed names"。

namespace pitot::names {

inline constexpr wchar_t kSingleInstanceMutex[] = L"Local\\Pitot.SingleInstance";
// 计划任务 \Pitot\Autostart；任务计划 API 要文件夹和任务名分开给，三个常量必须保持一致
inline constexpr wchar_t kAutostartTask[] = L"\\Pitot\\Autostart";
inline constexpr wchar_t kAutostartFolder[] = L"\\Pitot";
inline constexpr wchar_t kAutostartName[] = L"Autostart";
inline constexpr wchar_t kEtwSession[] = L"Pitot-Trace";
inline constexpr wchar_t kPawnIoDevice[] = L"\\\\?\\GLOBALROOT\\Device\\PawnIO";
inline constexpr wchar_t kPawnIoService[] = L"PawnIO";
inline constexpr wchar_t kOfferPawnIoArg[] = L"--offer-pawnio";
inline constexpr wchar_t kInstallPawnIoArg[] = L"--install-pawnio";
inline constexpr wchar_t kPawnIoInstallMutex[] = L"Local\\Pitot.PawnIoInstall";

// 与 FanControl / LibreHardwareMonitor 共用，D-003
inline constexpr wchar_t kPciMutex[] = L"Global\\Access_PCI";
inline constexpr wchar_t kIsaBusMutex[] = L"Global\\Access_ISABUS.HTP.Method";

inline constexpr wchar_t kMainWindowClass[] = L"Pitot.Main";
// 设置窗口另起进程（Pitot.exe --settings，D-042），有自己的单实例互斥量；窗口类名用来找已打开的那个
inline constexpr wchar_t kSettingsArg[] = L"--settings";
inline constexpr wchar_t kSettingsMutex[] = L"Local\\Pitot.Settings";
inline constexpr wchar_t kSettingsWindowClass[] = L"Pitot.Settings";

// 配置：%APPDATA%\Pitot\config.ini
inline constexpr wchar_t kAppDataFolder[] = L"Pitot";
inline constexpr wchar_t kConfigFile[] = L"config.ini";
inline constexpr wchar_t kLogFile[] = L"pitot.log";
// 今日 / 本月流量：%LOCALAPPDATA%\Pitot\traffic.dat（不放配置目录：那里每次写入都会触发配置重读）
inline constexpr wchar_t kTrafficFile[] = L"traffic.dat";

// 主进程把当前的传感器列表放在这块共享内存里，设置窗口的“显示与重命名”读它（D-070）
inline constexpr wchar_t kSensorShareMapping[] = L"Local\\Pitot.Sensors";
// 崩溃转储：%LOCALAPPDATA%\Pitot\crash-<时间>.dmp，最多留几份（D-078）
inline constexpr wchar_t kCrashDumpPrefix[] = L"crash-";
inline constexpr int kCrashDumpsKept = 5;
// Pitot.exe --demo 用虚构的数据代替采样结果，给文档截图用（D-083）
inline constexpr wchar_t kDemoArg[] = L"--demo";
// Pitot.exe --crash-test 故意崩溃一次，用来验收崩溃记录（D-078）
inline constexpr wchar_t kCrashTestArg[] = L"--crash-test";

}  // namespace pitot::names
