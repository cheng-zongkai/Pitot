#include "config/config.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iterator>

#include "common/unique_handle.h"

namespace pitot::config {
namespace {

constexpr wchar_t kGeneral[] = L"general";
constexpr wchar_t kStrip[] = L"strip";
constexpr wchar_t kFlyout[] = L"flyout";
constexpr wchar_t kNetwork[] = L"network";
constexpr wchar_t kSensors[] = L"sensors";
constexpr wchar_t kThemeSwitch[] = L"theme_switch";
// 区分“键不存在”和“键存在但为空”：空名称是合法的（用户可能想去掉标签）
constexpr wchar_t kMissing[] = L"\x01";

constexpr wchar_t kHeaderZh[] =
    L"; Pitot 配置文件。修改并保存后立即生效，不用重启。\r\n"
    L"; 尺寸单位都是 px（100% 缩放下的值，高缩放时自动放大）。删掉某一行就恢复默认值。\r\n"
    L"; 注释必须单独占一行，不能写在值的后面。\r\n";

constexpr wchar_t kGeneralZh[] =
    L"[general]\r\n"
    L"; 界面语言：auto 跟随 Windows 显示语言，zh-CN 中文，en-US 英文\r\n"
    L"language = auto\r\n"
    L"; 主题：system 跟随系统，light 浅色，dark 深色。只影响浮窗和设置窗口；\r\n"
    L"; 任务栏条始终跟随任务栏本身的颜色，否则文字会看不清\r\n"
    L"theme = system\r\n"
    L"; 强调色（占用竖条、选中态等）：system 跟随系统强调色，或者 #RRGGBB，例如 #0067C0\r\n"
    L"accent = system\r\n"
    L"; 也可以给浅色、深色分别指定，留空就用上面的 accent\r\n"
    L"accent_light =\r\n"
    L"accent_dark =\r\n"
    L"; 刷新间隔（秒，可带一位小数，如 1.5）：基础指标（CPU、内存、网速、进程）1 到 10；传感器（温度、风扇、功耗）1 到 30\r\n"
    L"interval = 1\r\n"
    L"sensor_interval = 2\r\n";

constexpr wchar_t kStripZh[] =
    L"[strip]\r\n"
    L"; 样式：a 双行堆叠，b 迷你曲线，c 单行极简\r\n"
    L"style = a\r\n"
    L"; 位置：left 任务栏左侧空白（任务栏图标要居中），right 通知区左边\r\n"
    L"position = left\r\n"
    L"; 副显示器的任务栏上也显示：1 开，0 关\r\n"
    L"all_taskbars = 0\r\n"
    L"; 网速单位：auto 字节自动进位，bits 比特自动进位（Mbps），kb 固定 KB/s，mb 固定 MB/s\r\n"
    L"net_unit = auto\r\n"
    L"; 字体，填系统里已安装的字体名；找不到时用 Segoe UI\r\n"
    L"font = Segoe UI Variable Text\r\n"
    L"; 字号：标签（CPU、RAM）、数值（23%）、网速\r\n"
    L"label_size = 10\r\n"
    L"value_size = 13\r\n"
    L"net_size = 11\r\n"
    L"; 数值字重：400 常规，600 半粗，700 粗体\r\n"
    L"value_weight = 600\r\n"
    L"; 左右内边距\r\n"
    L"padding = 10\r\n"
    L"; 没单独写间距的项，前面留这么宽（两行速率的网速、磁盘除外）。设置页在宽度条上拖\r\n"
    L"item_gap = 10\r\n"
    L"; 网速、磁盘读写和前一项之间的间距\r\n"
    L"net_gap = 14\r\n"
    L"; 竖条和文字之间的间距\r\n"
    L"bar_text_gap = 6\r\n"
    L"; 单击：item 打开点中的那一项对应的面板；flyout 按 [flyout] open_panel 打开\r\n"
    L"click = item\r\n"
    L"; 显示项，从左到右用逗号分开，最多 8 项，同一来源可以出现多次。\r\n"
    L"; 可用：cpu、memory、gpu、temp（等于 temp:cpu）、temp:gpu、temp:storage、temp:board、\r\n"
    L"; disk_activity（磁盘活动百分比）、disk（磁盘读写速率）、net。\r\n"
    L"; 后面加 +bar / -bar 打开或关掉竖条（cpu、memory、gpu、disk_activity 可用），\r\n"
    L"; 加 =名称 改显示名称，= 后面留空表示不显示名称，例如 temp:gpu=显卡, gpu+bar。\r\n"
    L"; 某一项前面的间距写成 ~px，只影响这一段，例如 memory~6, net~20；不写就用上面的默认间距\r\n"
    L"; 任务栏条最宽到开始按钮前；全部去掉时恢复 CPU、内存、网速\r\n"
    L"items = cpu, memory, net\r\n";

constexpr wchar_t kFlyoutZh[] =
    L"[flyout]\r\n"
    L"; 进程列表显示几行，3 到 10\r\n"
    L"process_rows = 8\r\n"
    L"; 点浮窗外面时收起：1 开，0 关\r\n"
    L"auto_close = 1\r\n"
    L"; 打开时显示：last 上次看的面板，或 cpu、gpu、memory、storage、network、sensors\r\n"
    L"open_panel = last\r\n"
    L"; 显示哪些磁贴，逗号分开：cpu、gpu、memory、storage、network、sensors\r\n"
    L"tiles = cpu, gpu, memory, storage, network, sensors\r\n"
    L"; 趋势线覆盖多长时间（秒）：60、300、900 或 3600。只在内存里记，重启后从头开始\r\n"
    L"history = 60\r\n";

constexpr wchar_t kSensorsZh[] =
    L"[sensors]\r\n"
    L"; 温度着色（℃）：从 warn 起显示橙色，从 hot 起显示红色。改成华氏度显示时这里仍写摄氏度\r\n"
    L"warn = 60\r\n"
    L"hot = 80\r\n"
    L"; 温度单位：c 摄氏度，f 华氏度\r\n"
    L"unit = c\r\n"
    L"; 高温通知：最高温度在 hot 以上持续 10 秒时发一条系统通知。1 开，0 关\r\n"
    L"notify = 0\r\n"
    L"; 单个传感器可以隐藏或改名，每个一节，例如：\r\n"
    L"; [sensor.nct6687d.0x104]\r\n"
    L"; visible = 1\r\n"
    L"; alias = 供电模块\r\n";

constexpr wchar_t kHeaderEn[] =
    L"; Pitot settings. Changes take effect as soon as you save; no restart needed.\r\n"
    L"; Sizes are in px at 100% scaling and scale up automatically. Delete a line to restore its default.\r\n"
    L"; Comments must be on their own line, not after a value.\r\n";

constexpr wchar_t kGeneralEn[] =
    L"[general]\r\n"
    L"; Language: auto follows the Windows display language, zh-CN Chinese, en-US English\r\n"
    L"language = auto\r\n"
    L"; Theme: system, light or dark. Applies to the flyout and settings window only;\r\n"
    L"; the taskbar strip always follows the taskbar colors so the text stays readable\r\n"
    L"theme = system\r\n"
    L"; Accent color (usage bars, selection): system, or #RRGGBB such as #0067C0\r\n"
    L"accent = system\r\n"
    L"; Optional separate colors for light and dark mode; leave empty to use accent\r\n"
    L"accent_light =\r\n"
    L"accent_dark =\r\n"
    L"; Refresh interval in seconds, one decimal allowed (e.g. 1.5): basic metrics (CPU, memory,\r\n"
    L"; network, processes) 1 to 10; sensors (temperatures, fans, power) 1 to 30\r\n"
    L"interval = 1\r\n"
    L"sensor_interval = 2\r\n";

constexpr wchar_t kStripEn[] =
    L"[strip]\r\n"
    L"; Style: a two stacked rows, b mini graphs, c single line\r\n"
    L"style = a\r\n"
    L"; Position: left uses the empty area on the left (taskbar icons centered), right sits left of\r\n"
    L"; the notification area\r\n"
    L"position = left\r\n"
    L"; Also show on the taskbars of other monitors: 1 on, 0 off\r\n"
    L"all_taskbars = 0\r\n"
    L"; Network speed unit: auto (bytes), bits (Mbps), kb (always KB/s), mb (always MB/s)\r\n"
    L"net_unit = auto\r\n"
    L"; Font name as installed on this PC; falls back to Segoe UI\r\n"
    L"font = Segoe UI Variable Text\r\n"
    L"; Font sizes: labels (CPU, RAM), values (23%), network speed\r\n"
    L"label_size = 10\r\n"
    L"value_size = 13\r\n"
    L"net_size = 11\r\n"
    L"; Value font weight: 400 regular, 600 semibold, 700 bold\r\n"
    L"value_weight = 600\r\n"
    L"; Left and right padding\r\n"
    L"padding = 10\r\n"
    L"; Default gap before an item, except a two-line rate. Drag a single gap on the width bar in Settings\r\n"
    L"item_gap = 10\r\n"
    L"; Gap before network speed and disk read/write\r\n"
    L"net_gap = 14\r\n"
    L"; Gap between the bar and its text\r\n"
    L"bar_text_gap = 6\r\n"
    L"; Click: item opens the panel for what you clicked; flyout follows [flyout] open_panel\r\n"
    L"click = item\r\n"
    L"; Items from left to right, comma-separated, at most 8; a source may appear more than once.\r\n"
    L"; Available: cpu, memory, gpu, temp (= temp:cpu), temp:gpu, temp:storage, temp:board,\r\n"
    L"; disk_activity (highest disk active time, percent), disk (read/write speed), net.\r\n"
    L"; Append +bar / -bar to turn the bar on or off (cpu, memory, gpu, disk_activity), and\r\n"
    L"; =name to rename an item (nothing after = hides the name), e.g. temp:gpu=GPU, gpu+bar.\r\n"
    L"; ~px sets the gap before that one item only, e.g. memory~6, net~20; omit it to use the default\r\n"
    L"; The strip can grow up to the Start button; turning every item off restores CPU, memory\r\n"
    L"; and network\r\n"
    L"items = cpu, memory, net\r\n";

constexpr wchar_t kFlyoutEn[] =
    L"[flyout]\r\n"
    L"; Number of rows in process lists, 3 to 10\r\n"
    L"process_rows = 8\r\n"
    L"; Hide the flyout when you click outside it: 1 on, 0 off\r\n"
    L"auto_close = 1\r\n"
    L"; Panel shown on open: last, or cpu, gpu, memory, storage, network, sensors\r\n"
    L"open_panel = last\r\n"
    L"; Tiles to show, comma-separated: cpu, gpu, memory, storage, network, sensors\r\n"
    L"tiles = cpu, gpu, memory, storage, network, sensors\r\n"
    L"; Time covered by the trend lines, in seconds: 60, 300, 900 or 3600. Kept in memory only\r\n"
    L"history = 60\r\n";

constexpr wchar_t kSensorsEn[] =
    L"[sensors]\r\n"
    L"; Temperature colors (C): orange from warn, red from hot. Always in Celsius, even when shown in F\r\n"
    L"warn = 60\r\n"
    L"hot = 80\r\n"
    L"; Temperature unit: c Celsius, f Fahrenheit\r\n"
    L"unit = c\r\n"
    L"; Hot notification: a Windows notification when the highest temperature stays at or above hot\r\n"
    L"; for 10 seconds. 1 on, 0 off\r\n"
    L"notify = 0\r\n"
    L"; Each sensor can be hidden or renamed in its own section, for example:\r\n"
    L"; [sensor.nct6687d.0x104]\r\n"
    L"; visible = 1\r\n"
    L"; alias = VRM\r\n";

// 浮窗右上角的主题切换按钮（D-065）
constexpr wchar_t kThemeSwitchZh[] =
    L"[theme_switch]\r\n"
    L"; 浮窗右上角的主题切换按钮：切到浅色 / 深色时，Windows 模式和壁纸一起换\r\n"
    L"; 壁纸写完整路径，可以用 %USERPROFILE% 等环境变量；留空表示切换时不动壁纸\r\n"
    L"light_wallpaper =\r\n"
    L"dark_wallpaper =\r\n";

constexpr wchar_t kThemeSwitchEn[] =
    L"[theme_switch]\r\n"
    L"; Windows theme button in the top-right corner of the flyout: switching between light and\r\n"
    L"; dark also changes the Windows mode and the wallpaper\r\n"
    L"; Full path; environment variables such as %USERPROFILE% are expanded. Empty keeps the current wallpaper\r\n"
    L"light_wallpaper =\r\n"
    L"dark_wallpaper =\r\n";

constexpr wchar_t kNetworkZh[] =
    L"[network]\r\n"
    L"; 统计哪块网卡：留空是自动（全部物理网卡）。在浮窗网络面板点网卡按钮选择，会写到这里\r\n"
    L"adapter =\r\n"
    L"; 网卡列表里显示虚拟网卡（VPN、TUN、WSL 等）：1 显示，0 不显示\r\n"
    L"show_virtual = 1\r\n"
    L"; 记录今日 / 本月流量：1 开，0 关\r\n"
    L"usage = 1\r\n"
    L"; 每月几号开始重新统计流量，1 到 28\r\n"
    L"reset_day = 1\r\n"
    L"; 网络延迟检测：1 开，0 关\r\n"
    L"ping = 0\r\n"
    L"; 延迟检测目标，IP 地址或域名\r\n"
    L"ping_target = 1.1.1.1\r\n";

constexpr wchar_t kNetworkEn[] =
    L"[network]\r\n"
    L"; Adapter to measure: empty means auto (all physical adapters). Pick one with the adapter button\r\n"
    L"; in the flyout's Network panel and it is saved here\r\n"
    L"adapter =\r\n"
    L"; List virtual adapters (VPN, TUN, WSL...): 1 show, 0 hide\r\n"
    L"show_virtual = 1\r\n"
    L"; Record daily / monthly data usage: 1 on, 0 off\r\n"
    L"usage = 1\r\n"
    L"; Day of the month data usage starts over, 1 to 28\r\n"
    L"reset_day = 1\r\n"
    L"; Network latency check: 1 on, 0 off\r\n"
    L"ping = 0\r\n"
    L"; Latency target, an IP address or host name\r\n"
    L"ping_target = 1.1.1.1\r\n";

struct SectionTemplate {
    const wchar_t* name;
    const wchar_t* zh;
    const wchar_t* en;
};

// 新增一节时只要加在这里：新用户的模板和老用户的追加都会带上它
constexpr SectionTemplate kSections[] = {
    {kGeneral, kGeneralZh, kGeneralEn},
    {kStrip, kStripZh, kStripEn},
    {kFlyout, kFlyoutZh, kFlyoutEn},
    {kNetwork, kNetworkZh, kNetworkEn},
    {kSensors, kSensorsZh, kSensorsEn},
    {kThemeSwitch, kThemeSwitchZh, kThemeSwitchEn},
};

// 已有节里后来新增的键：老配置文件缺了就补上默认值（不带注释，WritePrivateProfileString 写不了注释）
struct KeyDefault {
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* value;
};
constexpr KeyDefault kAddedKeys[] = {
    {kGeneral, L"language", L"auto"},
    {kGeneral, L"interval", L"1"},
    {kGeneral, L"sensor_interval", L"2"},
    {kFlyout, L"auto_close", L"1"},
    {kFlyout, L"open_panel", L"last"},
    {kStrip, L"click", L"item"},
    {kStrip, L"style", L"a"},
    {kStrip, L"position", L"left"},
    {kStrip, L"all_taskbars", L"0"},
    {kStrip, L"net_unit", L"auto"},
    {kFlyout, L"tiles", L"cpu, gpu, memory, storage, network, sensors"},
    {kFlyout, L"history", L"60"},
    {kSensors, L"unit", L"c"},
    {kSensors, L"notify", L"0"},
};
constexpr wchar_t kSensorPrefix[] = L"sensor.";
void Trim(wchar_t* s) {
    size_t len = wcslen(s);
    while (len > 0 && iswspace(s[len - 1])) {
        s[--len] = L'\0';
    }
    size_t start = 0;
    while (s[start] && iswspace(s[start])) {
        ++start;
    }
    if (start > 0) {
        memmove(s, s + start, (wcslen(s + start) + 1) * sizeof(wchar_t));
    }
}

// 读字符串；返回 false 表示键不存在。去掉首尾空白。
template <size_t N>
bool ReadString(const wchar_t* path, const wchar_t* section, const wchar_t* key, wchar_t (&out)[N]) {
    wchar_t buf[N];
    GetPrivateProfileStringW(section, key, kMissing, buf, static_cast<DWORD>(N), path);
    if (wcscmp(buf, kMissing) == 0) {
        return false;
    }
    Trim(buf);
    wcsncpy_s(out, buf, _TRUNCATE);
    return true;
}

// 读数字；缺失或不是数字时保持原值，超出范围时截断
void ReadNumber(const wchar_t* path, const wchar_t* section, const wchar_t* key, float min, float max,
                float& value) {
    wchar_t buf[64];
    if (!ReadString(path, section, key, buf)) {
        return;
    }
    wchar_t* end = nullptr;
    const double parsed = wcstod(buf, &end);
    if (end == buf || std::isnan(parsed)) {
        return;
    }
    value = static_cast<float>(std::clamp(parsed, static_cast<double>(min), static_cast<double>(max)));
}

// 开关：0 关，其他数字开；缺失或不是数字时保持原值
void ReadFlag(const wchar_t* path, const wchar_t* section, const wchar_t* key, bool& value) {
    float v = value ? 1.0f : 0.0f;
    ReadNumber(path, section, key, 0.0f, 1.0f, v);
    value = v != 0.0f;
}

std::optional<uint32_t> ReadColor(const wchar_t* path, const wchar_t* key) {
    wchar_t buf[32];
    return ReadString(path, kGeneral, key, buf) ? ParseColor(buf) : std::nullopt;
}

bool WriteText(HANDLE file, const wchar_t* text) {
    const auto bytes = static_cast<DWORD>(wcslen(text) * sizeof(wchar_t));
    DWORD written = 0;
    return WriteFile(file, text, bytes, &written, nullptr) && written == bytes;
}

bool IsUtf16File(const wchar_t* path) {
    unique_file_handle file(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    BYTE bom[2]{};
    DWORD read = 0;
    return file && ReadFile(file.get(), bom, sizeof(bom), &read, nullptr) && read == 2 && bom[0] == 0xFF &&
           bom[1] == 0xFE;
}

bool HasSection(const wchar_t* names, const wchar_t* section) {
    for (const wchar_t* p = names; *p; p += wcslen(p) + 1) {
        if (_wcsicmp(p, section) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::optional<uint32_t> ParseColor(const wchar_t* text) {
    wchar_t buf[32];
    wcsncpy_s(buf, text ? text : L"", _TRUNCATE);
    Trim(buf);
    const wchar_t* hex = buf[0] == L'#' ? buf + 1 : buf;
    if (wcslen(hex) != 6) {
        return std::nullopt;
    }
    uint32_t value = 0;
    for (const wchar_t* p = hex; *p; ++p) {
        if (!iswxdigit(*p)) {
            return std::nullopt;
        }
        value = value * 16 + static_cast<uint32_t>(iswdigit(*p) ? *p - L'0' : towlower(*p) - L'a' + 10);
    }
    return value;
}

// 旧版的六项顺序（D-057、D-059），只在迁移老配置文件时用到；power 不再作为独立项
constexpr const wchar_t* kLegacyItemKeys[] = {L"cpu", L"temp", L"power", L"gpu", L"memory", L"net"};

static bool TokenIs(const wchar_t* start, const wchar_t* end, const wchar_t* want) {
    const size_t len = wcslen(want);
    return static_cast<size_t>(end - start) == len && _wcsnicmp(start, want, len) == 0;
}

static const wchar_t* SourceToken(const StripItemConfig& item) {
    switch (item.source) {
    case StripSource::Cpu:
        return L"cpu";
    case StripSource::Memory:
        return L"memory";
    case StripSource::Gpu:
        return L"gpu";
    case StripSource::Temp:
        switch (item.temp) {
        case StripTempSource::Cpu:
            return L"temp";
        case StripTempSource::Gpu:
            return L"temp:gpu";
        case StripTempSource::Storage:
            return L"temp:storage";
        case StripTempSource::Board:
            return L"temp:board";
        }
        return L"temp";
    case StripSource::Disk:
        return L"disk";
    case StripSource::DiskActivity:
        return L"disk_activity";
    case StripSource::Net:
        return L"net";
    case StripSource::Empty:
        break;
    }
    return L"";
}

bool StripSourceHasBar(StripSource source) {
    return source == StripSource::Cpu || source == StripSource::Memory || source == StripSource::Gpu ||
           source == StripSource::DiskActivity;
}

bool StripSourceHasLabel(StripSource source) {
    return source != StripSource::Empty && source != StripSource::Disk && source != StripSource::Net;
}

bool DefaultStripBar(StripSource source) { return StripSourceHasBar(source) && source != StripSource::Gpu; }

bool operator==(const StripItemConfig& a, const StripItemConfig& b) {
    return a.source == b.source && a.temp == b.temp && a.bar == b.bar && a.label_set == b.label_set &&
           wcscmp(a.label, b.label) == 0 && a.gap_set == b.gap_set && (!a.gap_set || a.gap == b.gap);
}

// 一项的来源部分（不含 +bar 和 =名称）
static bool ParseSourceToken(const wchar_t* start, const wchar_t* end, StripItemConfig& item) {
    if (TokenIs(start, end, L"cpu")) {
        item.source = StripSource::Cpu;
    } else if (TokenIs(start, end, L"memory") || TokenIs(start, end, L"ram")) {
        item.source = StripSource::Memory;
    } else if (TokenIs(start, end, L"gpu")) {
        item.source = StripSource::Gpu;
    } else if (TokenIs(start, end, L"disk")) {
        item.source = StripSource::Disk;
    } else if (TokenIs(start, end, L"disk_activity") || TokenIs(start, end, L"diskactivity")) {
        item.source = StripSource::DiskActivity;
    } else if (TokenIs(start, end, L"net") || TokenIs(start, end, L"network")) {
        item.source = StripSource::Net;
    } else if (TokenIs(start, end, L"temp")) {
        item.source = StripSource::Temp;
    } else if (end - start > 5 && _wcsnicmp(start, L"temp:", 5) == 0) {
        const wchar_t* sub = start + 5;
        if (TokenIs(sub, end, L"cpu")) {
            item.temp = StripTempSource::Cpu;
        } else if (TokenIs(sub, end, L"gpu")) {
            item.temp = StripTempSource::Gpu;
        } else if (TokenIs(sub, end, L"storage") || TokenIs(sub, end, L"ssd") || TokenIs(sub, end, L"disk")) {
            item.temp = StripTempSource::Storage;
        } else if (TokenIs(sub, end, L"board")) {
            item.temp = StripTempSource::Board;
        } else {
            return false;  // 不认识的温度来源整项丢掉
        }
        item.source = StripSource::Temp;
    } else {
        return false;
    }
    return true;
}

static void TrimRange(const wchar_t*& start, const wchar_t*& end) {
    while (start < end && (*start == L' ' || *start == L'\t')) {
        ++start;
    }
    while (end > start && (end[-1] == L' ' || end[-1] == L'\t')) {
        --end;
    }
}

void ParseStripItems(const wchar_t* text, StripItemConfig (&out)[kStripItemCount]) {
    for (StripItemConfig& item : out) {
        item = {};
    }
    int n = 0;
    const wchar_t* p = text ? text : L"";
    while (*p && n < kStripItemCount) {
        while (*p == L' ' || *p == L'\t' || *p == L',') {
            ++p;
        }
        if (!*p) {
            break;
        }
        const wchar_t* start = p;
        while (*p && *p != L',') {
            ++p;
        }
        const wchar_t* end = p;
        // 第一个 = 之后都是名称，名称里可以再有 =
        const wchar_t* equals = start;
        while (equals < end && *equals != L'=') {
            ++equals;
        }
        const wchar_t* source_end = equals;
        TrimRange(start, source_end);
        // ~间距 写在 +bar 之后、=名称 之前，例如 cpu+bar~6
        bool gap_set = false;
        float gap = 0;
        const wchar_t* tilde = nullptr;
        for (const wchar_t* c = start; c + 1 < source_end; ++c) {
            if (*c == L'~') {
                tilde = c;
            }
        }
        if (tilde) {
            wchar_t* endp = nullptr;
            const double value = wcstod(tilde + 1, &endp);
            if (endp == source_end && !std::isnan(value)) {
                gap_set = true;
                gap = static_cast<float>(std::clamp(value, 0.0, static_cast<double>(kMaxStripSpacing)));
                source_end = tilde;
                TrimRange(start, source_end);
            }
        }
        std::optional<bool> bar;
        if (source_end - start > 4 && _wcsnicmp(source_end - 4, L"+bar", 4) == 0) {
            bar = true;
            source_end -= 4;
        } else if (source_end - start > 4 && _wcsnicmp(source_end - 4, L"-bar", 4) == 0) {
            bar = false;
            source_end -= 4;
        }
        TrimRange(start, source_end);
        StripItemConfig item;
        if (!ParseSourceToken(start, source_end, item)) {
            continue;
        }
        item.bar = StripSourceHasBar(item.source) && bar.value_or(DefaultStripBar(item.source));
        item.gap_set = gap_set;
        item.gap = gap;
        if (equals < end && StripSourceHasLabel(item.source)) {
            const wchar_t* label = equals + 1;
            const wchar_t* label_end = end;
            TrimRange(label, label_end);
            const size_t len = std::min(static_cast<size_t>(label_end - label), std::size(item.label) - 1);
            wmemcpy(item.label, label, len);
            item.label[len] = L'\0';
            item.label_set = true;
        }
        out[n++] = item;
    }
}

void FormatStripItems(const StripItemConfig (&items)[kStripItemCount], wchar_t* out, size_t count) {
    if (!out || count == 0) {
        return;
    }
    out[0] = L'\0';
    bool first = true;
    for (const StripItemConfig& item : items) {
        if (item.source == StripSource::Empty) {
            continue;
        }
        wchar_t token[64];
        wcscpy_s(token, SourceToken(item));
        if (StripSourceHasBar(item.source) && item.bar != DefaultStripBar(item.source)) {
            wcscat_s(token, item.bar ? L"+bar" : L"-bar");
        }
        if (item.gap_set) {
            wchar_t gap[24];
            _snwprintf_s(gap, _TRUNCATE, L"~%.*g", std::numeric_limits<float>::max_digits10,
                         static_cast<double>(item.gap));
            wcscat_s(token, gap);
        }
        if (item.label_set && StripSourceHasLabel(item.source)) {
            wcscat_s(token, L"=");
            size_t len = wcslen(token);
            // 逗号是项之间的分隔符，名称里不能有
            for (const wchar_t* c = item.label; *c && len + 1 < std::size(token); ++c) {
                if (*c != L',') {
                    token[len++] = *c;
                }
            }
            token[len] = L'\0';
        }
        const size_t need = wcslen(token) + (first ? 0 : 2);
        if (wcslen(out) + need >= count) {
            break;
        }
        if (!first) {
            wcscat_s(out, count, L", ");
        }
        first = false;
        wcscat_s(out, count, token);
    }
}

void CompactStripItems(StripItemConfig (&items)[kStripItemCount]) {
    int n = 0;
    for (int i = 0; i < kStripItemCount; ++i) {
        if (items[i].source != StripSource::Empty) {
            if (n != i) {
                items[n] = items[i];
            }
            ++n;
        }
    }
    for (int i = n; i < kStripItemCount; ++i) {
        items[i] = {};
    }
}

int CountStripItems(const StripItemConfig (&items)[kStripItemCount]) {
    int n = 0;
    for (const StripItemConfig& item : items) {
        if (item.source != StripSource::Empty) {
            ++n;
        }
    }
    return n;
}

bool HasStripItems(const StripItemConfig (&items)[kStripItemCount]) { return CountStripItems(items) > 0; }

uint8_t ParseTiles(const wchar_t* text) {
    unsigned tiles = 0;
    const wchar_t* p = text ? text : L"";
    while (*p) {
        while (*p == L' ' || *p == L'\t' || *p == L',') {
            ++p;
        }
        const wchar_t* start = p;
        while (*p && *p != L',') {
            ++p;
        }
        const wchar_t* end = p;
        TrimRange(start, end);
        for (int i = 0; i < static_cast<int>(std::size(kPanelKeys)); ++i) {
            if (TokenIs(start, end, kPanelKeys[i])) {
                tiles |= 1u << i;
            }
        }
    }
    return static_cast<uint8_t>(tiles != 0 ? tiles : 0x3Fu);
}

void FormatTiles(uint8_t tiles, wchar_t* out, size_t count) {
    if (!out || count == 0) {
        return;
    }
    out[0] = L'\0';
    for (int i = 0; i < static_cast<int>(std::size(kPanelKeys)); ++i) {
        if (tiles & (1u << i)) {
            if (out[0]) {
                wcscat_s(out, count, L", ");
            }
            wcscat_s(out, count, kPanelKeys[i]);
        }
    }
}

const SensorSetting* FindSensorSetting(const SensorsConfig& sensors, const wchar_t* key) {
    for (int i = 0; i < sensors.setting_count; ++i) {
        if (_wcsicmp(sensors.settings[i].key, key) == 0) {
            return &sensors.settings[i];
        }
    }
    return nullptr;
}

bool SensorVisible(const SensorsConfig& sensors, const wchar_t* key) {
    const SensorSetting* setting = FindSensorSetting(sensors, key);
    return !setting || setting->visible;
}

const wchar_t* SensorName(const SensorsConfig& sensors, const wchar_t* key, const wchar_t* fallback) {
    const SensorSetting* setting = FindSensorSetting(sensors, key);
    return setting && setting->alias[0] ? setting->alias : fallback;
}

bool WriteSensorSetting(const wchar_t* ini_path, const wchar_t* key, bool visible, const wchar_t* alias) {
    if (GetFileAttributesW(ini_path) != INVALID_FILE_ATTRIBUTES && !IsUtf16File(ini_path)) {
        return false;
    }
    wchar_t section[64];
    _snwprintf_s(section, _TRUNCATE, L"%s%s", kSensorPrefix, key);
    // 默认状态不留一节，免得配置文件越来越长
    if (visible && (!alias || !alias[0])) {
        return WritePrivateProfileStringW(section, nullptr, nullptr, ini_path) != FALSE;
    }
    return WritePrivateProfileStringW(section, L"visible", visible ? L"1" : L"0", ini_path) &&
           WritePrivateProfileStringW(section, L"alias", alias ? alias : L"", ini_path);
}

Config LoadConfig(const wchar_t* ini_path) {
    Config c;
    if (GetFileAttributesW(ini_path) == INVALID_FILE_ATTRIBUTES) {
        return c;
    }

    // [general]
    wchar_t language[16];
    if (ReadString(ini_path, kGeneral, L"language", language)) {
        if (_wcsnicmp(language, L"zh", 2) == 0) {
            c.general.language = LanguageMode::Chinese;
        } else if (_wcsnicmp(language, L"en", 2) == 0) {
            c.general.language = LanguageMode::English;
        }
    }
    wchar_t theme[16];
    if (ReadString(ini_path, kGeneral, L"theme", theme)) {
        if (_wcsicmp(theme, L"light") == 0) {
            c.general.theme = ThemeMode::Light;
        } else if (_wcsicmp(theme, L"dark") == 0) {
            c.general.theme = ThemeMode::Dark;
        }
    }
    c.general.accent = ReadColor(ini_path, L"accent");
    c.general.accent_light = ReadColor(ini_path, L"accent_light");
    c.general.accent_dark = ReadColor(ini_path, L"accent_dark");
    // 秒数取到 0.1 s，存成毫秒
    const auto to_ms = [](float seconds) { return static_cast<int>(std::lround(seconds * 10.0f)) * 100; };
    float interval = static_cast<float>(c.general.interval_ms) / 1000.0f;
    ReadNumber(ini_path, kGeneral, L"interval", 1.0f, 10.0f, interval);
    c.general.interval_ms = to_ms(interval);
    float sensor_interval = static_cast<float>(c.general.sensor_interval_ms) / 1000.0f;
    ReadNumber(ini_path, kGeneral, L"sensor_interval", 1.0f, 30.0f, sensor_interval);
    c.general.sensor_interval_ms = to_ms(sensor_interval);

    // [strip]
    StripStyle& s = c.strip;
    wchar_t font[std::size(s.font)];
    if (ReadString(ini_path, kStrip, L"font", font) && font[0] != L'\0') {
        wcscpy_s(s.font, font);
    }
    ReadNumber(ini_path, kStrip, L"label_size", 6.0f, 32.0f, s.label_size);
    ReadNumber(ini_path, kStrip, L"value_size", 6.0f, 32.0f, s.value_size);
    ReadNumber(ini_path, kStrip, L"net_size", 6.0f, 32.0f, s.net_size);
    float weight = static_cast<float>(s.value_weight);
    ReadNumber(ini_path, kStrip, L"value_weight", 100.0f, 900.0f, weight);
    s.value_weight = static_cast<int>(weight);
    // 旧版的全局名称 label_cpu / label_memory（D-057）。名称现在跟着每一项走（D-068），
    // 没写名称的 CPU、内存项沿用旧值；设置窗口下次写 items 时删掉这两个键
    wchar_t legacy_cpu[kStripLabelChars];
    wchar_t legacy_memory[kStripLabelChars];
    bool legacy_cpu_set = ReadString(ini_path, kStrip, L"label_cpu", legacy_cpu);
    bool legacy_memory_set = ReadString(ini_path, kStrip, L"label_memory", legacy_memory);
    // 旧版模板直接写了 label_cpu = CPU，不是用户自己改的：当作没写（D-029）。RAM 是两种语言共同的默认名称
    // （D-032），写的是它也当没写。旧模板的 label_memory = 内存 不特殊处理：写“内存”就是用户想要“内存”
    if (legacy_cpu_set && wcscmp(legacy_cpu, L"CPU") == 0) {
        legacy_cpu_set = false;
    }
    if (legacy_memory_set && wcscmp(legacy_memory, L"RAM") == 0) {
        legacy_memory_set = false;
    }
    ReadNumber(ini_path, kStrip, L"padding", 0.0f, kMaxStripSpacing, s.padding);
    ReadNumber(ini_path, kStrip, L"item_gap", 0.0f, kMaxStripSpacing, s.item_gap);
    ReadNumber(ini_path, kStrip, L"net_gap", 0.0f, kMaxStripSpacing, s.net_gap);
    ReadNumber(ini_path, kStrip, L"bar_text_gap", 0.0f, kMaxStripSpacing, s.bar_text_gap);
    wchar_t click[16];
    if (ReadString(ini_path, kStrip, L"click", click)) {
        s.click_item = _wcsicmp(click, L"flyout") != 0;
    }
    wchar_t word[16];
    if (ReadString(ini_path, kStrip, L"style", word)) {
        s.variant = _wcsicmp(word, L"b") == 0   ? StripVariant::Spark
                    : _wcsicmp(word, L"c") == 0 ? StripVariant::Minimal
                                                : StripVariant::Stack;
    }
    if (ReadString(ini_path, kStrip, L"position", word)) {
        s.position = _wcsicmp(word, L"right") == 0 ? StripPosition::Right : StripPosition::Left;
    }
    if (ReadString(ini_path, kStrip, L"net_unit", word)) {
        s.net_unit = _wcsicmp(word, L"bits") == 0 ? NetUnit::Bits
                     : _wcsicmp(word, L"kb") == 0 ? NetUnit::Kilo
                     : _wcsicmp(word, L"mb") == 0 ? NetUnit::Mega
                                                  : NetUnit::Auto;
    }
    ReadFlag(ini_path, kStrip, L"all_taskbars", s.all_taskbars);
    // 显示项（D-066）。新配置只写 items；老配置没有它时，从 show_* 和 order 迁移。
    // 旧版的 power 项不再存在，迁移时丢掉
    wchar_t items_text[kStripItemsTextChars];
    if (ReadString(ini_path, kStrip, L"items", items_text)) {
        ParseStripItems(items_text, s.items);
    } else {
        bool legacy[6] = {true, false, false, false, true, true};  // 默认 CPU、温度关、功耗关、GPU 关、内存、网速
        ReadFlag(ini_path, kStrip, L"show_cpu", legacy[0]);
        ReadFlag(ini_path, kStrip, L"show_temp", legacy[1]);
        ReadFlag(ini_path, kStrip, L"show_power", legacy[2]);
        ReadFlag(ini_path, kStrip, L"show_gpu", legacy[3]);
        ReadFlag(ini_path, kStrip, L"show_memory", legacy[4]);
        ReadFlag(ini_path, kStrip, L"show_net", legacy[5]);
        int legacy_order[6] = {0, 1, 2, 3, 4, 5};
        wchar_t order_text[96];
        if (ReadString(ini_path, kStrip, L"order", order_text)) {
            bool seen[6] = {};
            int n = 0;
            const wchar_t* p = order_text;
            while (*p && n < 6) {
                while (*p == L' ' || *p == L'\t' || *p == L',') {
                    ++p;
                }
                if (!*p) {
                    break;
                }
                const wchar_t* start = p;
                while (*p && *p != L',') {
                    ++p;
                }
                const wchar_t* end = p;
                while (end > start && (end[-1] == L' ' || end[-1] == L'\t')) {
                    --end;
                }
                for (int i = 0; i < 6; ++i) {
                    if (!seen[i] && TokenIs(start, end, kLegacyItemKeys[i])) {
                        legacy_order[n++] = i;
                        seen[i] = true;
                        break;
                    }
                }
            }
            for (int i = 0; i < 6 && n < 6; ++i) {
                if (!seen[i]) {
                    legacy_order[n++] = i;
                }
            }
        }
        int n = 0;
        for (int slot : legacy_order) {
            if (!legacy[slot] || n >= kStripItemCount) {
                continue;
            }
            switch (slot) {
            case 0:
                s.items[n++] = {StripSource::Cpu, StripTempSource::Cpu, true};
                break;
            case 1:
                s.items[n++] = {StripSource::Temp, StripTempSource::Cpu};
                break;
            case 3:
                s.items[n++] = {StripSource::Gpu, StripTempSource::Cpu};
                break;
            case 4:
                s.items[n++] = {StripSource::Memory, StripTempSource::Cpu, true};
                break;
            case 5:
                s.items[n++] = {StripSource::Net, StripTempSource::Cpu};
                break;
            default:
                break;  // 2 是旧的封装功耗项，不再单独显示
            }
        }
    }
    CompactStripItems(s.items);
    // 全部去掉时任务栏条没有可点的内容，恢复默认三项
    if (!HasStripItems(s.items)) {
        const StripStyle defaults;
        std::copy(std::begin(defaults.items), std::end(defaults.items), std::begin(s.items));
    }
    for (StripItemConfig& item : s.items) {
        const bool cpu = item.source == StripSource::Cpu && legacy_cpu_set;
        const bool memory = item.source == StripSource::Memory && legacy_memory_set;
        if (!item.label_set && (cpu || memory)) {
            wcscpy_s(item.label, cpu ? legacy_cpu : legacy_memory);
            item.label_set = true;
        }
    }

    // [flyout]
    float rows = static_cast<float>(c.flyout.process_rows);
    ReadNumber(ini_path, kFlyout, L"process_rows", static_cast<float>(kMinProcessRows),
               static_cast<float>(kMaxProcessRows), rows);
    c.flyout.process_rows = static_cast<int>(rows);
    ReadFlag(ini_path, kFlyout, L"auto_close", c.flyout.auto_close);
    wchar_t open_panel[16];
    if (ReadString(ini_path, kFlyout, L"open_panel", open_panel)) {
        c.flyout.open_panel = -1;
        for (int i = 0; i < static_cast<int>(std::size(kPanelKeys)); ++i) {
            if (_wcsicmp(open_panel, kPanelKeys[i]) == 0) {
                c.flyout.open_panel = i;
            }
        }
    }
    wchar_t tiles[96];
    if (ReadString(ini_path, kFlyout, L"tiles", tiles)) {
        c.flyout.tiles = ParseTiles(tiles);
    }
    float history = static_cast<float>(c.flyout.history_seconds);
    ReadNumber(ini_path, kFlyout, L"history", static_cast<float>(kMinHistorySeconds),
               static_cast<float>(kMaxHistorySeconds), history);
    c.flyout.history_seconds = static_cast<int>(history);

    // [sensors]
    float warn = static_cast<float>(c.sensors.warn);
    float hot = static_cast<float>(c.sensors.hot);
    ReadNumber(ini_path, kSensors, L"warn", 30.0f, 120.0f, warn);
    ReadNumber(ini_path, kSensors, L"hot", 30.0f, 120.0f, hot);
    c.sensors.warn = static_cast<int>(warn);
    // 高温阈值不低于警告阈值
    c.sensors.hot = std::max(static_cast<int>(hot), c.sensors.warn);
    wchar_t unit[8];
    if (ReadString(ini_path, kSensors, L"unit", unit)) {
        c.sensors.fahrenheit = _wcsicmp(unit, L"f") == 0;
    }
    ReadFlag(ini_path, kSensors, L"notify", c.sensors.notify);
    // [sensor.<键>]：枚举全部节名，挑出前缀对的
    wchar_t names[4096];
    if (GetPrivateProfileSectionNamesW(names, static_cast<DWORD>(std::size(names)), ini_path) > 0) {
        const size_t prefix = wcslen(kSensorPrefix);
        for (const wchar_t* p = names; *p && c.sensors.setting_count < kMaxSensorSettings; p += wcslen(p) + 1) {
            if (_wcsnicmp(p, kSensorPrefix, prefix) != 0 || p[prefix] == L'\0' ||
                wcslen(p + prefix) >= static_cast<size_t>(kSensorKeyChars)) {
                continue;
            }
            SensorSetting& setting = c.sensors.settings[c.sensors.setting_count];
            setting = {};
            wcscpy_s(setting.key, p + prefix);
            for (wchar_t* k = setting.key; *k; ++k) {
                *k = static_cast<wchar_t>(towlower(*k));
            }
            ReadFlag(ini_path, p, L"visible", setting.visible);
            ReadString(ini_path, p, L"alias", setting.alias);
            ++c.sensors.setting_count;
        }
    }

    // [network]
    NetworkConfig& n = c.network;
    wchar_t adapter[64];
    if (ReadString(ini_path, kNetwork, L"adapter", adapter)) {
        n.adapter = ParseGuid(adapter);
    }
    ReadFlag(ini_path, kNetwork, L"show_virtual", n.show_virtual);
    ReadFlag(ini_path, kNetwork, L"usage", n.usage);
    float reset_day = static_cast<float>(n.reset_day);
    ReadNumber(ini_path, kNetwork, L"reset_day", 1.0f, static_cast<float>(kMaxResetDay), reset_day);
    n.reset_day = static_cast<int>(reset_day);
    ReadFlag(ini_path, kNetwork, L"ping", n.ping);
    wchar_t target[std::size(n.ping_target)];
    if (ReadString(ini_path, kNetwork, L"ping_target", target) && target[0] != L'\0') {
        wcscpy_s(n.ping_target, target);
    }

    // [theme_switch]：没写的键保持默认路径；写空表示该模式不换壁纸
    wchar_t wallpaper[MAX_PATH];
    if (ReadString(ini_path, kThemeSwitch, L"light_wallpaper", wallpaper)) {
        wcscpy_s(c.theme_switch.light_wallpaper, wallpaper);
    }
    if (ReadString(ini_path, kThemeSwitch, L"dark_wallpaper", wallpaper)) {
        wcscpy_s(c.theme_switch.dark_wallpaper, wallpaper);
    }
    return c;
}

std::optional<GUID> ParseGuid(const wchar_t* text) {
    wchar_t buf[64];
    wcsncpy_s(buf, text ? text : L"", _TRUNCATE);
    Trim(buf);
    const wchar_t* p = buf;
    size_t len = wcslen(p);
    if (len == 38 && p[0] == L'{' && p[37] == L'}') {
        ++p;
        len = 36;
    }
    if (len != 36 || p[8] != L'-' || p[13] != L'-' || p[18] != L'-' || p[23] != L'-') {
        return std::nullopt;
    }
    // 去掉连字符后是 32 个十六进制数字，按 Data1 / Data2 / Data3 / Data4[8] 的顺序
    uint8_t bytes[16]{};
    int n = 0;
    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue;
        }
        if (!iswxdigit(p[i])) {
            return std::nullopt;
        }
        const auto digit = static_cast<uint8_t>(iswdigit(p[i]) ? p[i] - L'0' : towlower(p[i]) - L'a' + 10);
        bytes[n / 2] = static_cast<uint8_t>(bytes[n / 2] << 4 | digit);
        ++n;
    }
    GUID g{};
    g.Data1 = static_cast<unsigned long>(bytes[0]) << 24 | static_cast<unsigned long>(bytes[1]) << 16 |
              static_cast<unsigned long>(bytes[2]) << 8 | bytes[3];
    g.Data2 = static_cast<unsigned short>(bytes[4] << 8 | bytes[5]);
    g.Data3 = static_cast<unsigned short>(bytes[6] << 8 | bytes[7]);
    for (int i = 0; i < 8; ++i) {
        g.Data4[i] = bytes[8 + i];
    }
    return g;
}

void FormatGuid(const GUID& g, wchar_t* out, size_t count) {
    _snwprintf_s(out, count, _TRUNCATE, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g.Data1, g.Data2,
                 g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
                 g.Data4[7]);
}

bool WriteValue(const wchar_t* ini_path, const wchar_t* section, const wchar_t* key, const wchar_t* value) {
    if (GetFileAttributesW(ini_path) != INVALID_FILE_ATTRIBUTES && !IsUtf16File(ini_path)) {
        return false;
    }
    return WritePrivateProfileStringW(section, key, value, ini_path) != FALSE;
}

bool WriteNetworkAdapter(const wchar_t* ini_path, const std::optional<GUID>& adapter) {
    wchar_t value[40] = L"";
    if (adapter) {
        FormatGuid(*adapter, value, std::size(value));
    }
    return WriteValue(ini_path, kNetwork, L"adapter", value);
}

bool WriteDefaultConfig(const wchar_t* ini_path, bool english) {
    // CREATE_NEW：已有文件绝不覆盖
    unique_file_handle file(
        CreateFileW(ini_path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        return false;
    }
    if (!WriteText(file.get(), L"\xFEFF") || !WriteText(file.get(), english ? kHeaderEn : kHeaderZh)) {
        return false;
    }
    for (const auto& section : kSections) {
        if (!WriteText(file.get(), L"\r\n") || !WriteText(file.get(), english ? section.en : section.zh)) {
            return false;
        }
    }
    return true;
}

int AppendMissingSections(const wchar_t* ini_path, bool english) {
    if (GetFileAttributesW(ini_path) == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    if (!IsUtf16File(ini_path)) {
        return -1;
    }
    wchar_t names[4096];
    const DWORD length = GetPrivateProfileSectionNamesW(names, static_cast<DWORD>(std::size(names)), ini_path);
    if (length == 0) {
        names[0] = names[1] = L'\0';
    }

    unique_file_handle file;
    int appended = 0;
    for (const auto& section : kSections) {
        if (HasSection(names, section.name)) {
            continue;
        }
        if (!file) {
            file.reset(CreateFileW(ini_path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!file) {
                return appended;
            }
        }
        if (WriteText(file.get(), L"\r\n") && WriteText(file.get(), english ? section.en : section.zh)) {
            ++appended;
        }
    }
    file.reset();

    // 节都在了，再补节里缺的键
    for (const auto& k : kAddedKeys) {
        wchar_t value[8];
        if (!ReadString(ini_path, k.section, k.key, value) &&
            WritePrivateProfileStringW(k.section, k.key, k.value, ini_path)) {
            ++appended;
        }
    }
    return appended;
}

}  // namespace pitot::config
