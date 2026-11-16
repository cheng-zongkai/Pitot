#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

// config.ini 的内容（D-010、D-022）。文件是 UTF-16LE 带 BOM，用 GetPrivateProfileStringW 读。
// 尺寸单位都是 DIP（100% 缩放下的 px），绘制时按 DPI 放大。

namespace pitot::config {

enum class ThemeMode { System, Light, Dark };
// 界面语言：auto 跟随 Windows 显示语言（中文用中文，其余用英文）
enum class LanguageMode { Auto, Chinese, English };

// [general]
struct GeneralConfig {
    LanguageMode language = LanguageMode::Auto;
    // 浮窗和设置窗口的深浅色。任务栏条不受它影响，始终跟随任务栏本身，否则文字会和任务栏背景撞色。
    ThemeMode theme = ThemeMode::System;
    // 强调色 0xRRGGBB；空表示用系统强调色（D-021）
    std::optional<uint32_t> accent;
    // 浅色 / 深色模式下单独指定，优先于 accent
    std::optional<uint32_t> accent_light;
    std::optional<uint32_t> accent_dark;
    // 刷新间隔，毫秒（INI 里按秒写，可带一位小数）：基础指标（CPU、内存、网速、进程）1 / 1.5 / 2 / 5 s；
    // 传感器（温度、风扇、功耗）2 / 5 / 10 s。传感器间隔按基础间隔的整数倍走，不足一倍时按一倍
    int interval_ms = 1000;
    int sensor_interval_ms = 2000;
};

// [strip] 任务栏条每一项的数值来源（D-066、D-067）。Empty 表示这一格不用
// Disk 是两行读写速率；DiskActivity 是竖条 + 百分比，宽度和 CPU、内存一样小
enum class StripSource : uint8_t { Empty, Cpu, Memory, Gpu, Temp, Disk, DiskActivity, Net };
// 温度项读哪个传感器；默认标签跟着来源走（CPU / GPU / SSD / 主板）
enum class StripTempSource : uint8_t { Cpu, Gpu, Storage, Board };
// 显示项最多几项。真正的上限是开始按钮前的空白（D-068），这里只限制配置的长度
inline constexpr int kStripItemCount = 8;
inline constexpr int kStripTempSourceCount = 4;
inline constexpr int kStripLabelChars = 16;  // 含结尾的 0
// 给叠加多段间距和缩放后的浮点坐标留出范围。
inline constexpr float kMaxStripSpacing = std::numeric_limits<float>::max() / (4 * kStripItemCount);

// 百分比项（CPU、内存、GPU、磁盘活动）可以带竖条
bool StripSourceHasBar(StripSource source);
// 两行速率项（网速、磁盘读写）没有名称
bool StripSourceHasLabel(StripSource source);
// 新加或换了来源时竖条的默认值：GPU 默认不带，和 D-057 一样省宽度
bool DefaultStripBar(StripSource source);

struct StripItemConfig {
    StripSource source = StripSource::Empty;
    StripTempSource temp = StripTempSource::Cpu;
    bool bar = false;  // 只对 StripSourceHasBar 的来源有意义
    // 显示名称。label_set 为假用界面语言的默认名称；为真且为空表示不显示名称（D-068）
    bool label_set = false;
    wchar_t label[kStripLabelChars] = L"";
    // 这一项前面的间距。gap_set 为假时用 item_gap，两行速率项用 net_gap（D-069）
    bool gap_set = false;
    float gap = 0;
};
bool operator==(const StripItemConfig& a, const StripItemConfig& b);

// [strip] style：A 双行堆叠、B 迷你曲线、C 单行极简（D-075）
enum class StripVariant : uint8_t { Stack, Spark, Minimal };
// [strip] position：任务栏左侧空白，或通知区左边（D-074）
enum class StripPosition : uint8_t { Left, Right };
// [strip] net_unit：任务栏条网速的单位（D-073）。Auto 是字节、自动进位（最小 KB/s，D-036）；
// Bits 是比特、自动进位（Kbps、Mbps）；Kilo / Mega 固定 KB/s / MB/s
enum class NetUnit : uint8_t { Auto, Bits, Kilo, Mega };

// [strip] 任务栏条外观
struct StripStyle {
    StripVariant variant = StripVariant::Stack;
    StripPosition position = StripPosition::Left;
    NetUnit net_unit = NetUnit::Auto;
    // 副显示器的任务栏上也显示（D-077）
    bool all_taskbars = false;

    wchar_t font[64] = L"Segoe UI Variable Text";
    float label_size = 10.0f;  // “CPU”“内存”
    float value_size = 13.0f;  // “23%”
    float net_size = 11.0f;    // 网速两行
    int value_weight = 600;

    float padding = 10.0f;      // 可点击区左右内边距
    float item_gap = 10.0f;     // 相邻两项之间（设计稿 14，用户反馈偏宽）
    float net_gap = 14.0f;      // 两行速率项（网速、磁盘）前面的间距
    float bar_text_gap = 6.0f;  // 竖条和文字之间

    // 单击行为：真时点中哪一项就打开对应面板；假时按 [flyout] open_panel 打开（D-044）
    bool click_item = true;

    // 从左到右的显示项，空的排在末尾（D-066）。默认 CPU、内存、网速；全空时 LoadConfig 恢复这三项
    StripItemConfig items[kStripItemCount] = {
        {StripSource::Cpu, StripTempSource::Cpu, true},
        {StripSource::Memory, StripTempSource::Cpu, true},
        {StripSource::Net, StripTempSource::Cpu, false},
    };
};

// "cpu, temp:gpu=显卡, gpu+bar, memory~6, net~20"：按从左到右的顺序，最多 kStripItemCount 项。
// +bar / -bar 改竖条，~px 是这一项前面的间距（不写就用默认），=名称 改显示名称（= 后面留空是不显示名称）。
// 不认识的词丢掉，空项排在末尾
void ParseStripItems(const wchar_t* text, StripItemConfig (&out)[kStripItemCount]);
// 只写和默认值不同的部分。名称里的逗号会被去掉
void FormatStripItems(const StripItemConfig (&items)[kStripItemCount], wchar_t* out, size_t count);
inline constexpr size_t kStripItemsTextChars = 512;
void CompactStripItems(StripItemConfig (&items)[kStripItemCount]);
// 至少有一项不是 Empty
bool HasStripItems(const StripItemConfig (&items)[kStripItemCount]);
int CountStripItems(const StripItemConfig (&items)[kStripItemCount]);

// [flyout] 浮窗
struct FlyoutConfig {
    // 进程列表行数（CPU、内存等面板各自的列表）
    int process_rows = 8;
    // 点浮窗外面时收起（关掉后只能再点任务栏条或按 Esc 收起）
    bool auto_close = true;
    // 打开时显示哪个面板：-1 是上次看的，0–5 依次是 CPU、GPU、内存、存储、网络、传感器
    int open_panel = -1;
    // 显示哪几个磁贴，第 i 位对应 kPanelKeys[i]；至少留一个（D-072）
    uint8_t tiles = 0x3F;
    // 磁贴趋势线和任务栏条样式 B 的曲线覆盖多长时间，秒（D-076）
    int history_seconds = 60;
};
inline constexpr int kMinHistorySeconds = 60;
inline constexpr int kMaxHistorySeconds = 3600;
inline constexpr int kMinProcessRows = 3;
inline constexpr int kMaxProcessRows = 10;  // 与 sampler::kMaxTopProcesses 一致

// [network]（M7）
struct NetworkConfig {
    // 统计哪块网卡，按 InterfaceGuid 记（D-011）；为空是“自动”（全部物理网卡）
    std::optional<GUID> adapter;
    bool show_virtual = true;  // 网卡列表里列出 VPN、TUN 等虚拟网卡
    bool usage = true;         // 记录今日 / 本月流量
    int reset_day = 1;         // 每月几号开始重新统计
    bool ping = false;         // 延迟检测
    wchar_t ping_target[64] = L"1.1.1.1";
};
inline constexpr int kMaxResetDay = 28;

// [flyout] tiles 的写法："cpu, gpu, memory"。不认识的词丢掉，一个都没有时恢复全部
uint8_t ParseTiles(const wchar_t* text);
void FormatTiles(uint8_t tiles, wchar_t* out, size_t count);

// [flyout] open_panel 的取值，下标与浮窗面板编号一致；另有 "last"
inline constexpr const wchar_t* kPanelKeys[] = {L"cpu", L"gpu", L"memory", L"storage", L"network", L"sensors"};

// [sensors] 温度着色阈值（℃）：低于 warn 用强调色，warn 起橙色，hot 起红色
// [sensor.<键>] 每个传感器的显示开关和别名（D-070）。键见 sampler/sensor_keys.h，如 amd.tctl、nct6687d.0x104
inline constexpr int kMaxSensorSettings = 64;
inline constexpr int kSensorKeyChars = 24;
inline constexpr int kSensorAliasChars = 32;
struct SensorSetting {
    wchar_t key[kSensorKeyChars] = L"";
    bool visible = true;
    wchar_t alias[kSensorAliasChars] = L"";  // 空串表示用默认名称
};

// 阈值始终按 ℃ 存，界面上按 unit 换算（D-071）
struct SensorsConfig {
    int warn = 60;
    int hot = 80;
    bool fahrenheit = false;
    // 高温通知：最高温度在 hot 以上持续 10 秒时发一条系统通知（D-079）
    bool notify = false;
    int setting_count = 0;
    SensorSetting settings[kMaxSensorSettings]{};
};
// 没有单独设置时返回空
const SensorSetting* FindSensorSetting(const SensorsConfig& sensors, const wchar_t* key);
bool SensorVisible(const SensorsConfig& sensors, const wchar_t* key);
// 用户起的名称；没有时返回 fallback
const wchar_t* SensorName(const SensorsConfig& sensors, const wchar_t* key, const wchar_t* fallback);
// 写一个传感器的设置（设置窗口用）。可见且没有别名时整节删掉
bool WriteSensorSetting(const wchar_t* ini_path, const wchar_t* key, bool visible, const wchar_t* alias);

// [theme_switch] 浮窗右上角的主题切换按钮（D-065）。
// 切到浅色 / 深色时把当前用户的 Windows 模式换成对应的颜色，并换上这里的壁纸。
// 路径可以写 %USERPROFILE% 等环境变量；留空（默认）表示该模式下不动壁纸。
struct ThemeSwitchConfig {
    wchar_t light_wallpaper[MAX_PATH] = L"";
    wchar_t dark_wallpaper[MAX_PATH] = L"";
};

struct Config {
    GeneralConfig general;
    StripStyle strip;
    FlyoutConfig flyout;
    NetworkConfig network;
    SensorsConfig sensors;
    ThemeSwitchConfig theme_switch;
};

// 文件不存在、键缺失或值非法时用默认值；数值超出范围时截断
Config LoadConfig(const wchar_t* ini_path);

// 写一份带注释的完整默认配置。文件已存在时什么也不做，返回 false。
bool WriteDefaultConfig(const wchar_t* ini_path, bool english = false);

// 已有配置文件缺少某一节时（程序升级后新增的节），把该节的带注释模板追加到末尾，已有内容不动。
// 已有的节缺少后来新增的键（如 [general] language）时，用 WritePrivateProfileStringW 补上默认值。
// 返回追加的节数和键数之和；文件不是 UTF-16LE 时不敢追加，返回 -1。
int AppendMissingSections(const wchar_t* ini_path, bool english = false);

// "#RRGGBB" 或 "RRGGBB" → 0xRRGGBB；"system"、空串或格式不对时为空
std::optional<uint32_t> ParseColor(const wchar_t* text);

// "{6B29FC40-CA47-1067-B31D-00DD010662DA}"（花括号可省）→ GUID；格式不对时为空
std::optional<GUID> ParseGuid(const wchar_t* text);
// GUID → 带花括号的大写形式，out 至少 39 个字符
void FormatGuid(const GUID& guid, wchar_t* out, size_t count);

// 改一个键（设置窗口用）。文件不是 UTF-16LE 时不写，免得被 WritePrivateProfileString 按 ANSI 改写（D-022）
bool WriteValue(const wchar_t* ini_path, const wchar_t* section, const wchar_t* key, const wchar_t* value);

// 把网卡选择写回 config.ini（浮窗网络面板的网卡按钮用）。adapter 为空表示“自动”
bool WriteNetworkAdapter(const wchar_t* ini_path, const std::optional<GUID>& adapter);

}  // namespace pitot::config
