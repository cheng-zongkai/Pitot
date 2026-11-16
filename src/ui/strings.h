#pragma once

// 用户可见文字集中在这里（D-029）。每种语言一张 Strings 表，成员一一对应，
// 新增文字时两张表都要加（strings.cpp），测试会检查没有漏填。

#include "config/config.h"

namespace pitot::ui {

enum class Language { Chinese, English };

// 成员全是 const wchar_t* 或它的定长数组（测试按指针数组遍历检查非空），名字沿用旧常量名
struct Strings {
    // 不可用的数值（没有读数、接口失败）显示的占位符，区别于 0
    const wchar_t* kUnavailable;

    // 任务栏条（标签可以在 config.ini 里改）
    const wchar_t* kLabelCpu;
    const wchar_t* kLabelMemory;
    const wchar_t* kLabelGpu;
    const wchar_t* kLabelSsd;    // 磁盘温度项的标签
    const wchar_t* kLabelBoard;  // 主板温度项的标签
    const wchar_t* kLabelDisk;   // 磁盘活动项的标签
    const wchar_t* kDownloadLine;  // "↓ %s"
    const wchar_t* kArrowDown;     // 任务栏条网速的箭头，数字和单位分列画
    const wchar_t* kArrowUp;
    const wchar_t* kUploadLine;
    // 悬停提示按当前开着的项拼接。kTooltipFormat 是旧的整段，不再使用
    const wchar_t* kTooltipFormat;
    const wchar_t* kTooltipNamed;   // "%s %s"
    const wchar_t* kTooltipMemory;  // "%s %s（%s / %s）"
    const wchar_t* kTooltipDown;    // "下载 %s"
    const wchar_t* kTooltipUp;
    const wchar_t* kTooltipRead;    // "读取 %s"
    const wchar_t* kTooltipWrite;   // "写入 %s"
    // 任务栏条磁盘项的前缀：读 / 写，和网速的箭头区分开
    const wchar_t* kDiskReadShort;
    const wchar_t* kDiskWriteShort;
    const wchar_t* kMenuExit;

    // 运行时长
    const wchar_t* kUptimeDays;  // %llu 天 %llu 小时
    const wchar_t* kUptimeHours;
    const wchar_t* kUptimeMinutes;

    // 浮窗：标题栏、磁贴、状态栏
    const wchar_t* kFlyoutTitle;
    const wchar_t* kFlyoutLogo;  // 标题栏左上角的字标，全大写、拉开字距（D-048）
    const wchar_t* kTileCpu;
    const wchar_t* kTileGpu;
    const wchar_t* kTileMemory;
    const wchar_t* kTileStorage;
    const wchar_t* kTileNetwork;
    const wchar_t* kTileSensors;
    const wchar_t* kTemperatureMax;
    const wchar_t* kFooterTaskManager;

    // 传感器状态（状态栏、面板顶部提示）
    const wchar_t* kSensorsOk;
    const wchar_t* kSensorsStarting;
    const wchar_t* kSensorsNoDriver;
    const wchar_t* kSensorsUnsupported;
    const wchar_t* kSensorsFailed;
    const wchar_t* kSensorsPartial;

    // 浮窗右上角主题切换按钮的结果提示
    const wchar_t* kThemeSwitchFailed;

    // CPU 面板
    const wchar_t* kStatFrequency;
    const wchar_t* kStatPackagePower;
    const wchar_t* kStatTctl;
    const wchar_t* kStatCpuPackage;
    const wchar_t* kPerCoreTitle;

    const wchar_t* kEndProcess;
    const wchar_t* kEndProcessGroup;  // 合并行右键："结束 %d 个进程"
    const wchar_t* kEndConfirm;       // 单个进程："结束 %s（PID %lu）？…"
    const wchar_t* kEndConfirmGroup;  // 合并行："结束 %s 的 %d 个进程？…"
    const wchar_t* kEndDone;          // 结果提示："已结束 %s"
    const wchar_t* kEndDoneGroup;     // "已结束 %s（%d 个进程）"
    const wchar_t* kEndPartial;       // "已结束 %d 个进程，%d 个未能结束"
    const wchar_t* kEndFailed;
    const wchar_t* kCancel;
    const wchar_t* kEndButton;  // 确认卡片上的按钮，比“结束进程”短
    // 进程列表
    const wchar_t* kTopProcesses;
    const wchar_t* kProcessCollecting;
    const wchar_t* kProcessNameCount;  // "%s (%d)"
    const wchar_t* kProcessChild;      // 展开后的子进程行："%s · PID %lu"
    const wchar_t* kProcessMore;       // "还有 %d 个进程"

    // 内存面板
    const wchar_t* kMemInUse;
    const wchar_t* kMemModified;
    const wchar_t* kMemStandby;
    const wchar_t* kMemFree;
    const wchar_t* kMemCommitted;
    const wchar_t* kMemCompressed;
    const wchar_t* kMemSwapUsed;
    const wchar_t* kMemSpeed;
    const wchar_t* kCommitFormat;
    const wchar_t* kMemSpeedFormat;  // "%lu MT/s"

    // GPU 面板
    const wchar_t* kGpuNotFound;
    const wchar_t* kGpuNoAdlx;           // 没有 PitotAdlx.dll
    const wchar_t* kGpuAdlxUnavailable;  // 驱动没有 ADLX 或初始化失败
    const wchar_t* kGpuNoNvidia;         // 没有 PitotNvidia.dll
    const wchar_t* kGpuNvidiaUnavailable;  // 驱动里没有 nvml.dll 或初始化失败
    const wchar_t* kGpuNoActivity;         // 进程列表为空：没有进程在用这块显卡
    const wchar_t* kVramFormat;          // "%.1f / %.1f GB"
    const wchar_t* kMhzFormat;
    const wchar_t* kGpuHotspotRow;       // 温度列表里的“热点”
    const wchar_t* kGpuLoad3d;
    const wchar_t* kGpuClock;
    const wchar_t* kGpuBoardPower;
    const wchar_t* kGpuVram;
    const wchar_t* kGpuFan;
    const wchar_t* kGpuHotspot;
    const wchar_t* kGpuTemperatures;
    const wchar_t* kGpuCore;
    const wchar_t* kGpuVramTemp;
    const wchar_t* kColumnGpu;
    const wchar_t* kColumnVram;

    // 存储面板
    const wchar_t* kStorageNotFound;
    const wchar_t* kDiskUnnamed;   // 没有卷的盘："磁盘 %u"
    const wchar_t* kStorageActive;  // 读取 / 写入两列沿用 kColumnRead / kColumnWrite
    const wchar_t* kStorageTemp;
    const wchar_t* kColumnRead;
    const wchar_t* kColumnWrite;
    const wchar_t* kIoNoDiskActivity;  // 存储进程列表为空

    // 网络面板
    const wchar_t* kNetAdapterAuto;  // 自动 · %d 块物理网卡
    const wchar_t* kNetProcesses;    // 进程列表含所有网卡，标题点明范围
    const wchar_t* kNetDownload;
    const wchar_t* kNetUpload;
    const wchar_t* kNetLatency;
    const wchar_t* kIoNoNetActivity;
    const wchar_t* kNetMenuAuto;         // 网卡菜单第一项
    const wchar_t* kNetMenuAutoSub;      // 全部物理网卡（%d 块）
    const wchar_t* kNetNotConnected;
    const wchar_t* kNetFallbackMissing;  // 选定网卡找不到
    const wchar_t* kNetFallbackDown;     // 选定网卡没连上，%s 是网卡名
    const wchar_t* kTooltipNetAuto;      // 悬停提示最后一行：%d 块物理网卡
    const wchar_t* kTooltipNetAdapter;   // 悬停提示最后一行：%s 是网卡名
    const wchar_t* kPingMsFormat;  // "%lu ms"
    const wchar_t* kPingUnderOne;  // 往返不到 1 毫秒
    const wchar_t* kPingTimeout;
    const wchar_t* kPingUnreachable;
    const wchar_t* kPingUnresolved;
    const wchar_t* kTrafficToday;   // 网络面板流量两行的行名
    const wchar_t* kTrafficPeriod;

    // 按进程统计磁盘 / 网络（ETW）不可用的原因
    const wchar_t* kEtwNoAccess;
    const wchar_t* kEtwNoSlot;
    const wchar_t* kEtwFailed;

    // 传感器面板与数值格式
    const wchar_t* kGhzFormat;
    const wchar_t* kWattFormat;
    const wchar_t* kRpmFormat;
    const wchar_t* kSectionBoard;
    const wchar_t* kSectionFans;
    const wchar_t* kSectionGpu;
    const wchar_t* kSectionStorage;
    const wchar_t* kTctlTdie;
    const wchar_t* kCpuPackage;
    const wchar_t* kCcdFormat;
    // 主板温度通道 0x100 起，按通用部件命名（D-029）；之后的通道显示寄存器地址
    const wchar_t* kBoardTempNames[7];
    const wchar_t* kBoardTempUnnamed;  // 温度 0x%03X
    // 风扇通道 0x140 起
    const wchar_t* kFanNames[8];
    const wchar_t* kFanUnnamed;
    // 其他主板芯片（D-085）：ITE / Fintek 按槽位编号；Nuvoton bank 系按芯片的输入名（顺序见 sensors::NctSourceLabel）
    const wchar_t* kBoardTempNumbered;  // 温度 %d
    const wchar_t* kFanNumbered;        // 风扇 %d
    const wchar_t* kNctSourceNames[11];

    // 设置窗口（M7，D-042）。卡片没有说明文字时代码里传空指针，不在这里放空串
    const wchar_t* kSetTitle;
    const wchar_t* kSetNavGeneral;
    const wchar_t* kSetNavStrip;
    const wchar_t* kSetNavFlyout;
    const wchar_t* kSetNavSensors;
    const wchar_t* kSetNavNetwork;
    const wchar_t* kSetNavAbout;
    const wchar_t* kSetUndo;
    const wchar_t* kSetUndoFailed;
    const wchar_t* kSetOn;
    const wchar_t* kSetOff;
    const wchar_t* kSetFollowSystem;
    const wchar_t* kSetSecAppearance;
    const wchar_t* kSetTheme;
    const wchar_t* kSetThemeDesc;
    const wchar_t* kSetLight;
    const wchar_t* kSetDark;
    const wchar_t* kSetAccent;
    const wchar_t* kSetAccentDesc;
    const wchar_t* kSetCustom;
    const wchar_t* kSetAccentColor;
    const wchar_t* kSetAccentColorDesc;
    const wchar_t* kSetLanguage;
    const wchar_t* kSetFollowWindows;
    const wchar_t* kSetLangZh;
    const wchar_t* kSetLangEn;
    // 桌面主题切换（D-065）
    const wchar_t* kSetSecDesktopTheme;
    const wchar_t* kSetLightWallpaper;
    const wchar_t* kSetDarkWallpaper;
    const wchar_t* kSetChooseWallpaper;
    const wchar_t* kSetWallpaperNone;
    const wchar_t* kSetWallpaperFilter;  // 文件对话框的类型过滤器，用 \0 分隔
    const wchar_t* kSetSecStartup;
    const wchar_t* kSetAutostart;
    const wchar_t* kSetAutostartDesc;
    const wchar_t* kSetAutostartOtherPath;  // %s 是任务里登记的路径
    const wchar_t* kSetAutostartFailed;
    const wchar_t* kSetSecRefresh;
    const wchar_t* kSetInterval;
    const wchar_t* kSetIntervalDesc;
    const wchar_t* kSetSensorInterval;
    const wchar_t* kSetSensorIntervalDesc;
    const wchar_t* kSetSecondsFormat;  // "%g 秒"，参数是 double
    const wchar_t* kSetSecFont;
    const wchar_t* kSetFont;
    const wchar_t* kSetFontDesc;
    const wchar_t* kSetLabelSize;
    const wchar_t* kSetValueSize;
    const wchar_t* kSetNetSize;
    const wchar_t* kSetValueWeight;
    const wchar_t* kSetWeightRegular;
    const wchar_t* kSetWeightSemibold;
    const wchar_t* kSetWeightBold;
    const wchar_t* kSetLabelDesc;
    const wchar_t* kSetSecSpacing;
    const wchar_t* kSetPadding;
    const wchar_t* kSetNetGap;
    const wchar_t* kSetBarGap;
    const wchar_t* kSetPx;
    const wchar_t* kSetStripClick;
    const wchar_t* kSetClickItem;
    const wchar_t* kSetClickFlyout;
    const wchar_t* kSetSecItems;
    const wchar_t* kSetStripAdd;          // 末尾那张“添加”卡片
    const wchar_t* kSetStripSource;       // “数值来源”
    const wchar_t* kSetStripTempSource;   // “温度来源”
    const wchar_t* kSetSourceCpu;
    const wchar_t* kSetSourceMemory;
    const wchar_t* kSetSourceGpu;
    const wchar_t* kSetSourceTemp;
    const wchar_t* kSetSourceDisk;
    const wchar_t* kSetSourceDiskActivity;
    const wchar_t* kSetSourceNet;
    const wchar_t* kSetStripName;         // 每一项的显示名称
    const wchar_t* kSetStripBar;          // 每一项要不要竖条
    // 卡片上的名称：CPU、GPU 直接用 kLabelCpu / kLabelGpu，磁盘和网速用 kSetSource*
    const wchar_t* kSetChipMemory;
    const wchar_t* kSetChipCpuTemp;
    const wchar_t* kSetChipGpuTemp;
    const wchar_t* kSetChipSsdTemp;
    const wchar_t* kSetChipBoardTemp;
    const wchar_t* kSetStripUsed;
    const wchar_t* kSetStripRoom;  // "到开始按钮 %d px"
    const wchar_t* kSetStripTooWide;
    const wchar_t* kSetStripClipped;
    // 任务栏条靠右时（D-074）宽度条说的是图标区和通知区之间
    const wchar_t* kSetStripRoomIcons;
    const wchar_t* kSetStripTooWideIcons;
    const wchar_t* kSetStripClippedIcons;
    const wchar_t* kSetSecBehavior;
    const wchar_t* kSetOpenPanel;
    const wchar_t* kSetLastPanel;
    const wchar_t* kSetAutoClose;
    const wchar_t* kSetAutoCloseDesc;
    const wchar_t* kSetProcessRows;
    const wchar_t* kSetPawnIo;
    const wchar_t* kSetPawnIoDesc;
    const wchar_t* kInstallPawnIo;
    const wchar_t* kPawnIoPrompt;
    const wchar_t* kPawnIoInstalled;
    const wchar_t* kPawnIoFailed;
    const wchar_t* kSetDriverOk;
    const wchar_t* kSetDriverMissing;
    const wchar_t* kSetSecTemperature;
    const wchar_t* kSetWarn;
    const wchar_t* kSetWarnDesc;
    const wchar_t* kSetHot;
    const wchar_t* kSetHotDesc;
    const wchar_t* kSetCelsius;
    const wchar_t* kSetSecAdapter;
    const wchar_t* kSetAdapter;
    const wchar_t* kSetAdapterDesc;
    const wchar_t* kSetShowVirtual;
    const wchar_t* kSetShowVirtualDesc;
    const wchar_t* kSetSecUsage;
    const wchar_t* kSetUsage;
    const wchar_t* kSetUsageDesc;
    const wchar_t* kSetResetDay;
    const wchar_t* kSetDayFormat;  // "%d 日"
    const wchar_t* kSetSecLatency;
    const wchar_t* kSetPing;
    const wchar_t* kSetPingDesc;
    const wchar_t* kSetPingTarget;
    const wchar_t* kSetPingTargetDesc;
    const wchar_t* kSetVersionFormat;  // "版本 %s"
    const wchar_t* kSetLicense;
    const wchar_t* kSetLicenseDesc;
    const wchar_t* kSetThirdParty;
    const wchar_t* kSetThirdPartyDesc;
    const wchar_t* kSetLog;
    const wchar_t* kSetViewLog;
    const wchar_t* kSetLogOpenFailed;
    // °F，阈值输入框的单位
    const wchar_t* kSetFahrenheit;
    const wchar_t* kSetTempUnit;
    const wchar_t* kSetUnitCelsius;
    const wchar_t* kSetUnitFahrenheit;
    const wchar_t* kSetHotNotify;
    const wchar_t* kSetHotNotifyDesc;
    // 传感器页的“显示与重命名”（D-070）
    const wchar_t* kSetSecSensorList;
    const wchar_t* kSetSensorListDesc;
    const wchar_t* kSetSensorsNotRunning;
    const wchar_t* kSetSensorsEmpty;
    // 浮窗页：磁贴开关（D-072）
    const wchar_t* kSetSecTiles;
    // 趋势线时长（D-076）
    const wchar_t* kSetHistory;
    const wchar_t* kSetHistoryDesc;
    const wchar_t* kSetHistory1m;
    const wchar_t* kSetHistory5m;
    const wchar_t* kSetHistory15m;
    const wchar_t* kSetHistory1h;
    // 任务栏条页：样式、位置、多显示器、网速单位（D-073 至 D-077）
    const wchar_t* kSetStripStyle;
    const wchar_t* kSetStyleStack;
    const wchar_t* kSetStyleSpark;
    const wchar_t* kSetStyleMinimal;
    const wchar_t* kSetStripPosition;
    const wchar_t* kSetStripPositionDesc;
    const wchar_t* kSetPositionLeft;
    const wchar_t* kSetPositionRight;
    const wchar_t* kSetAllTaskbars;
    const wchar_t* kSetAllTaskbarsDesc;
    const wchar_t* kSetNetUnit;
    const wchar_t* kSetNetUnitAuto;
    const wchar_t* kSetNetUnitBits;
    const wchar_t* kSetNetUnitKilo;
    const wchar_t* kSetNetUnitMega;
    // 高温通知（D-079）
    const wchar_t* kHotTitle;
    // “%s 已到 %s，超过高温阈值 %s”
    const wchar_t* kHotBody;
};

// 当前语言的文字表。只在 UI 线程用
const Strings& Str();
void SetLanguage(Language language);
// 测试用：取某种语言的表
const Strings& StringsFor(Language language);

// Windows 显示语言是不是中文
bool SystemUsesChinese();
// [general] language → 实际用的语言：auto 时中文系统用中文，其余用英文
Language ResolveLanguage(config::LanguageMode mode);

// 任务栏条一项的默认名称：温度项按传感器来源显示 CPU / GPU / SSD / 主板；两行速率项没有名称（返回空串）
const wchar_t* StripItemDefaultLabel(const config::StripItemConfig& item);
// 任务栏条上实际显示的名称：用户改过就用用户的（可以是空串），否则用默认名称（D-068）
const wchar_t* StripItemLabel(const config::StripItemConfig& item);
// 设置页卡片上的名称：说清楚是哪个来源，不受自定义名称影响（“CPU 温度”“磁盘读写”）
const wchar_t* StripItemName(const config::StripItemConfig& item);

}  // namespace pitot::ui
