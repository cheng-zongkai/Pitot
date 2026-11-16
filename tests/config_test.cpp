#include "config/config.h"

#include <windows.h>

#include <cwchar>

#include "common/unique_handle.h"
#include "test.h"

namespace {

using pitot::config::AppendMissingSections;
using pitot::config::Config;
using pitot::config::LoadConfig;
using pitot::config::ParseColor;
using pitot::config::StripStyle;
using pitot::config::ThemeMode;
using pitot::config::WriteDefaultConfig;

// 测试用的临时 INI，析构时删除
class TempIni {
public:
    TempIni() {
        wchar_t dir[MAX_PATH];
        GetTempPathW(MAX_PATH, dir);
        GetTempFileNameW(dir, L"pit", 0, path_);
        DeleteFileW(path_);  // GetTempFileName 会建空文件，测试需要“不存在”的起点
    }
    ~TempIni() { DeleteFileW(path_); }
    TempIni(const TempIni&) = delete;
    TempIni& operator=(const TempIni&) = delete;

    const wchar_t* path() const { return path_; }

    // 默认以 UTF-16LE 带 BOM 写入，和程序生成的格式一致；utf8 = true 时模拟用户另存为 UTF-8
    void Write(const wchar_t* content, bool utf8 = false) const {
        pitot::unique_file_handle file(
            CreateFileW(path_, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD written = 0;
        if (utf8) {
            char buf[1024];
            const int n = WideCharToMultiByte(CP_UTF8, 0, content, -1, buf, sizeof(buf), nullptr, nullptr);
            WriteFile(file.get(), buf, static_cast<DWORD>(n - 1), &written, nullptr);
            return;
        }
        const wchar_t bom = 0xFEFF;
        WriteFile(file.get(), &bom, sizeof(bom), &written, nullptr);
        WriteFile(file.get(), content, static_cast<DWORD>(wcslen(content) * sizeof(wchar_t)), &written, nullptr);
    }

    DWORD Size() const {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        GetFileAttributesExW(path_, GetFileExInfoStandard, &data);
        return data.nFileSizeLow;
    }

private:
    wchar_t path_[MAX_PATH]{};
};

bool SameItems(const StripStyle& a, const StripStyle& b) {
    for (int i = 0; i < pitot::config::kStripItemCount; ++i) {
        if (!(a.items[i] == b.items[i])) {
            return false;
        }
    }
    return true;
}

bool SameStyle(const StripStyle& a, const StripStyle& b) {
    return wcscmp(a.font, b.font) == 0 && a.label_size == b.label_size && a.value_size == b.value_size &&
           a.net_size == b.net_size && a.value_weight == b.value_weight && a.padding == b.padding && a.item_gap == b.item_gap &&
           a.net_gap == b.net_gap && a.bar_text_gap == b.bar_text_gap && a.click_item == b.click_item &&
           SameItems(a, b);
}

bool SameNetwork(const pitot::config::NetworkConfig& n) {
    const pitot::config::NetworkConfig d;
    return !n.adapter && n.show_virtual == d.show_virtual && n.usage == d.usage && n.reset_day == d.reset_day &&
           n.ping == d.ping && wcscmp(n.ping_target, d.ping_target) == 0;
}

bool IsDefault(const Config& c) {
    return SameStyle(c.strip, StripStyle{}) && c.general.theme == ThemeMode::System && !c.general.accent &&
           !c.general.accent_light && !c.general.accent_dark &&
           c.flyout.process_rows == pitot::config::FlyoutConfig{}.process_rows && SameNetwork(c.network);
}

}  // namespace

TEST_CASE("config: missing file gives defaults") {
    TempIni ini;
    CHECK(IsDefault(LoadConfig(ini.path())));
}

TEST_CASE("config: default template round-trips to defaults") {
    TempIni ini;
    CHECK(WriteDefaultConfig(ini.path()));
    CHECK(IsDefault(LoadConfig(ini.path())));
    CHECK(AppendMissingSections(ini.path()) == 0);
}

TEST_CASE("config: template is never overwritten") {
    TempIni ini;
    ini.Write(L"[strip]\r\nitem_gap = 3\r\n");
    CHECK(!WriteDefaultConfig(ini.path()));
    CHECK(LoadConfig(ini.path()).strip.item_gap == 3.0f);
}

TEST_CASE("config: reads strip values and Chinese names") {
    TempIni ini;
    ini.Write(L"[strip]\r\nfont = 微软雅黑\r\nvalue_size = 14.5\r\nlabel_cpu = 处理器\r\n"
              L"label_memory =   内存占用  \r\nitem_gap = 6\r\nvalue_weight = 700\r\n");
    const StripStyle s = LoadConfig(ini.path()).strip;
    CHECK(wcscmp(s.font, L"微软雅黑") == 0);
    CHECK(s.value_size == 14.5f);
    // 旧版的全局名称并进了 CPU、内存这两项（D-068）
    CHECK(s.items[0].label_set);
    CHECK(wcscmp(s.items[0].label, L"处理器") == 0);
    CHECK(wcscmp(s.items[1].label, L"内存占用") == 0);
    CHECK(!s.items[2].label_set);
    CHECK(s.item_gap == 6.0f);
    CHECK(s.value_weight == 700);
    CHECK(s.net_gap == StripStyle{}.net_gap);  // 没写的键保持默认
    CHECK(s.items[0].source == pitot::config::StripSource::Cpu);
    CHECK(s.items[1].source == pitot::config::StripSource::Memory);
    CHECK(s.items[2].source == pitot::config::StripSource::Net);
    CHECK(s.items[3].source == pitot::config::StripSource::Empty);
}

TEST_CASE("config: strip items are a free list of value sources") {
    TempIni ini;
    ini.Write(L"[strip]\r\nitems = net, temp:gpu, disk_activity, disk, temp, temp:storage\r\n");
    const StripStyle s = LoadConfig(ini.path()).strip;
    CHECK(s.items[0].source == pitot::config::StripSource::Net);
    CHECK(s.items[1].source == pitot::config::StripSource::Temp);
    CHECK(s.items[1].temp == pitot::config::StripTempSource::Gpu);
    CHECK(s.items[2].source == pitot::config::StripSource::DiskActivity);
    CHECK(s.items[3].source == pitot::config::StripSource::Disk);
    CHECK(s.items[4].source == pitot::config::StripSource::Temp);
    CHECK(s.items[4].temp == pitot::config::StripTempSource::Cpu);
    CHECK(s.items[5].source == pitot::config::StripSource::Temp);
    CHECK(s.items[5].temp == pitot::config::StripTempSource::Storage);
    wchar_t text[pitot::config::kStripItemsTextChars];
    pitot::config::FormatStripItems(s.items, text, std::size(text));
    CHECK(wcscmp(text, L"net, temp:gpu, disk_activity, disk, temp, temp:storage") == 0);
}

TEST_CASE("config: strip items carry their own name and bar") {
    using pitot::config::StripSource;
    TempIni ini;
    ini.Write(L"[strip]\r\nitems = cpu-bar=处理器, gpu+bar, temp:gpu = 显卡 , memory=, net=忽略, "
              L"temp:board+bar, cpu, gpu, disk_activity, net\r\n");
    const StripStyle s = LoadConfig(ini.path()).strip;
    CHECK(s.items[0].source == StripSource::Cpu);
    CHECK(!s.items[0].bar);
    CHECK(wcscmp(s.items[0].label, L"处理器") == 0);
    CHECK(s.items[1].source == StripSource::Gpu);
    CHECK(s.items[1].bar);
    CHECK(!s.items[1].label_set);
    CHECK(s.items[2].temp == pitot::config::StripTempSource::Gpu);
    CHECK(wcscmp(s.items[2].label, L"显卡") == 0);
    // = 后面留空：不显示名称，和没写不同
    CHECK(s.items[3].label_set);
    CHECK(s.items[3].label[0] == L'\0');
    // 网速没有名称，温度没有竖条
    CHECK(!s.items[4].label_set);
    CHECK(!s.items[5].bar);
    // 同一来源可以出现多次，默认值按来源：CPU 带竖条，GPU 不带
    CHECK(s.items[6].source == StripSource::Cpu);
    CHECK(s.items[6].bar);
    CHECK(!s.items[7].bar);
    // 最多 8 项，多出来的丢掉
    CHECK(pitot::config::CountStripItems(s.items) == pitot::config::kStripItemCount);
    wchar_t text[pitot::config::kStripItemsTextChars];
    pitot::config::FormatStripItems(s.items, text, std::size(text));
    CHECK(wcscmp(text, L"cpu-bar=处理器, gpu+bar, temp:gpu=显卡, memory=, net, temp:board, cpu, gpu") == 0);

    // 名称里的逗号写出去时去掉，免得被当成下一项
    pitot::config::StripItemConfig items[pitot::config::kStripItemCount]{};
    items[0].source = StripSource::Temp;
    items[0].label_set = true;
    wcscpy_s(items[0].label, L"a,b");
    pitot::config::FormatStripItems(items, text, std::size(text));
    CHECK(wcscmp(text, L"temp=ab") == 0);
}

TEST_CASE("config: a strip gap belongs to one item") {
    using pitot::config::StripSource;
    TempIni ini;
    ini.Write(L"[strip]\r\nitems = cpu, memory~64, net~128.5, gpu+bar~0\r\n"
              L"padding = 80\r\nitem_gap = 96\r\nnet_gap = 120\r\nbar_text_gap = 72\r\n");
    const StripStyle s = LoadConfig(ini.path()).strip;
    CHECK(!s.items[0].gap_set);
    CHECK(s.items[1].gap_set);
    CHECK(s.items[1].gap == 64.0f);
    CHECK(s.items[2].source == StripSource::Net);
    CHECK(s.items[2].gap == 128.5f);
    CHECK(s.items[3].bar);
    CHECK(s.items[3].gap == 0.0f);
    CHECK(s.padding == 80.0f);
    CHECK(s.item_gap == 96.0f);
    CHECK(s.net_gap == 120.0f);
    CHECK(s.bar_text_gap == 72.0f);
    wchar_t text[pitot::config::kStripItemsTextChars];
    pitot::config::FormatStripItems(s.items, text, std::size(text));
    CHECK(wcscmp(text, L"cpu, memory~64, net~128.5, gpu+bar~0") == 0);
}

TEST_CASE("config: strip items drop unknown tokens and restore defaults when empty") {
    TempIni ini;
    ini.Write(L"[strip]\r\nitems = cpu, nope, temp:mars, gpu, net\r\n");
    StripStyle s = LoadConfig(ini.path()).strip;
    CHECK(s.items[0].source == pitot::config::StripSource::Cpu);
    CHECK(s.items[1].source == pitot::config::StripSource::Gpu);
    CHECK(s.items[2].source == pitot::config::StripSource::Net);
    CHECK(s.items[3].source == pitot::config::StripSource::Empty);
    ini.Write(L"[strip]\r\nitems =\r\n");
    s = LoadConfig(ini.path()).strip;
    CHECK(s.items[0].source == pitot::config::StripSource::Cpu);
    CHECK(s.items[1].source == pitot::config::StripSource::Memory);
    CHECK(s.items[2].source == pitot::config::StripSource::Net);
}

TEST_CASE("config: old show_/order keys migrate to the item list") {
    TempIni ini;
    ini.Write(L"[strip]\r\nshow_cpu = 0\r\nshow_gpu = 1\r\nshow_temp = 1\r\nshow_power = 1\r\n"
              L"order = gpu,cpu,memory,net,temp,power\r\n");
    const StripStyle s = LoadConfig(ini.path()).strip;
    // 旧的封装功耗项不再迁移；其余按旧顺序保留下来的项排好
    CHECK(s.items[0].source == pitot::config::StripSource::Gpu);
    CHECK(s.items[1].source == pitot::config::StripSource::Memory);
    CHECK(s.items[2].source == pitot::config::StripSource::Net);
    CHECK(s.items[3].source == pitot::config::StripSource::Temp);
    CHECK(s.items[3].temp == pitot::config::StripTempSource::Cpu);
    CHECK(s.items[4].source == pitot::config::StripSource::Empty);
    // 全部关掉时恢复默认三项
    ini.Write(L"[strip]\r\nshow_cpu = 0\r\nshow_memory = 0\r\nshow_net = 0\r\n"
              L"show_gpu = 0\r\nshow_temp = 0\r\nshow_power = 0\r\n");
    const StripStyle restored = LoadConfig(ini.path()).strip;
    CHECK(restored.items[0].source == pitot::config::StripSource::Cpu);
    CHECK(restored.items[1].source == pitot::config::StripSource::Memory);
    CHECK(restored.items[2].source == pitot::config::StripSource::Net);
    // 新键存在时以新键为准，旧键不再生效
    ini.Write(L"[strip]\r\nitems = disk\r\nshow_cpu = 0\r\nshow_gpu = 1\r\n");
    CHECK(LoadConfig(ini.path()).strip.items[0].source == pitot::config::StripSource::Disk);
}

TEST_CASE("config: empty name is allowed, empty font is not") {
    TempIni ini;
    ini.Write(L"[strip]\r\nlabel_cpu =\r\nfont =\r\n");
    const StripStyle s = LoadConfig(ini.path()).strip;
    CHECK(s.items[0].label_set);
    CHECK(s.items[0].label[0] == L'\0');
    CHECK(wcscmp(s.font, StripStyle{}.font) == 0);
}

TEST_CASE("config: out-of-range and invalid numbers") {
    TempIni ini;
    ini.Write(L"[strip]\r\nvalue_size = 200\r\nlabel_size = 1\r\nitem_gap = -5\r\nnet_gap = abc\r\n");
    const StripStyle s = LoadConfig(ini.path()).strip;
    CHECK(s.value_size == 32.0f);
    CHECK(s.label_size == 6.0f);
    CHECK(s.item_gap == 0.0f);
    CHECK(s.net_gap == StripStyle{}.net_gap);
}

TEST_CASE("config: parse color") {
    CHECK(ParseColor(L"#0067C0") == 0x0067C0u);
    CHECK(ParseColor(L" ff8800 ") == 0xFF8800u);
    CHECK(ParseColor(L"#abcdef") == 0xABCDEFu);
    CHECK(!ParseColor(L"system"));
    CHECK(!ParseColor(L""));
    CHECK(!ParseColor(L"#12345"));
    CHECK(!ParseColor(L"#12345G"));
    CHECK(!ParseColor(nullptr));
}

TEST_CASE("config: general section") {
    TempIni ini;
    ini.Write(L"[general]\r\ntheme = Dark\r\naccent = #FF8800\r\naccent_light =\r\naccent_dark = 00FF00\r\n");
    const Config c = LoadConfig(ini.path());
    CHECK(c.general.theme == ThemeMode::Dark);
    CHECK(c.general.accent == 0xFF8800u);
    CHECK(!c.general.accent_light);
    CHECK(c.general.accent_dark == 0x00FF00u);
}

TEST_CASE("config: unknown theme falls back to system") {
    TempIni ini;
    ini.Write(L"[general]\r\ntheme = purple\r\naccent = system\r\n");
    const Config c = LoadConfig(ini.path());
    CHECK(c.general.theme == ThemeMode::System);
    CHECK(!c.general.accent);
}

TEST_CASE("config: append missing sections keeps user values") {
    TempIni ini;
    ini.Write(L"; 用户的注释\r\n[strip]\r\nitem_gap = 4\r\nlabel_cpu = 处理器\r\n");
    // 缺五个节，已有的 [strip] 里补 click、style、position、all_taskbars、net_unit
    // （显示项用 items，新配置不追加，留给 LoadConfig 从旧键迁移）
    CHECK(AppendMissingSections(ini.path()) == 10);
    CHECK(AppendMissingSections(ini.path()) == 0);  // 第二次没有可追加的
    const Config c = LoadConfig(ini.path());
    CHECK(c.strip.item_gap == 4.0f);
    CHECK(wcscmp(c.strip.items[0].label, L"处理器") == 0);
    CHECK(c.general.theme == ThemeMode::System);
}

TEST_CASE("config: never append to a non UTF-16 file") {
    TempIni ini;
    ini.Write(L"[strip]\r\nitem_gap = 4\r\n", true);
    const DWORD before = ini.Size();
    CHECK(AppendMissingSections(ini.path()) == -1);
    CHECK(ini.Size() == before);
}

TEST_CASE("config: language option") {
    TempIni ini;
    ini.Write(L"[general]\r\nlanguage = en-US\r\n");
    CHECK(LoadConfig(ini.path()).general.language == pitot::config::LanguageMode::English);
    ini.Write(L"[general]\r\nlanguage = zh-CN\r\n");
    CHECK(LoadConfig(ini.path()).general.language == pitot::config::LanguageMode::Chinese);
    ini.Write(L"[general]\r\nlanguage = klingon\r\n");
    CHECK(LoadConfig(ini.path()).general.language == pitot::config::LanguageMode::Auto);
}

TEST_CASE("config: english template round-trips to defaults") {
    TempIni ini;
    CHECK(WriteDefaultConfig(ini.path(), true));
    CHECK(IsDefault(LoadConfig(ini.path())));
    CHECK(AppendMissingSections(ini.path(), true) == 0);
}

TEST_CASE("config: missing language key is added to an old general section") {
    TempIni ini;
    ini.Write(L"[general]\r\ntheme = dark\r\n\r\n[strip]\r\nitem_gap = 4\r\n\r\n[flyout]\r\n\r\n[network]\r\n\r\n[sensors]\r\n");
    // [general] 补 language、interval、sensor_interval；[strip] 补 click、style、position、all_taskbars、
    // net_unit；[flyout] 补 auto_close、open_panel、tiles、history；[sensors] 补 unit、notify；再加 theme_switch 一节
    CHECK(AppendMissingSections(ini.path()) == 15);
    CHECK(AppendMissingSections(ini.path()) == 0);
    const Config c = LoadConfig(ini.path());
    CHECK(c.general.language == pitot::config::LanguageMode::Auto);
    CHECK(c.general.theme == ThemeMode::Dark);
}

TEST_CASE("config: theme switch wallpapers") {
    TempIni ini;
    // 没写这一节时默认不换壁纸
    const Config empty = LoadConfig(ini.path());
    CHECK(empty.theme_switch.light_wallpaper[0] == L'\0');
    CHECK(empty.theme_switch.dark_wallpaper[0] == L'\0');

    ini.Write(L"[theme_switch]\r\nlight_wallpaper = D:\\壁纸\\light.jpg\r\ndark_wallpaper =\r\n");
    const Config c = LoadConfig(ini.path());
    CHECK(wcscmp(c.theme_switch.light_wallpaper, L"D:\\壁纸\\light.jpg") == 0);
    // 显式写空表示这个模式不换壁纸
    CHECK(c.theme_switch.dark_wallpaper[0] == L'\0');
}

TEST_CASE("config: process rows are clamped to 3-10") {
    TempIni ini;
    ini.Write(L"[flyout]\r\nprocess_rows = 6\r\n");
    CHECK(LoadConfig(ini.path()).flyout.process_rows == 6);
    ini.Write(L"[flyout]\r\nprocess_rows = 50\r\n");
    CHECK(LoadConfig(ini.path()).flyout.process_rows == 10);
    ini.Write(L"[flyout]\r\nprocess_rows = 1\r\n");
    CHECK(LoadConfig(ini.path()).flyout.process_rows == 3);
    ini.Write(L"[flyout]\r\nprocess_rows = many\r\n");
    CHECK(LoadConfig(ini.path()).flyout.process_rows == 8);
}

TEST_CASE("config: labels written by the old template follow the language") {
    TempIni ini;
    ini.Write(L"[strip]\r\nlabel_cpu = CPU\r\nlabel_memory = 内存\r\n");
    StripStyle s = LoadConfig(ini.path()).strip;
    CHECK(!s.items[0].label_set);
    CHECK(s.items[1].label_set);  // 默认已是 RAM，写“内存”就是要“内存”（D-032）
    CHECK(wcscmp(s.items[1].label, L"内存") == 0);
    ini.Write(L"[strip]\r\nlabel_cpu = 处理器\r\nlabel_memory =\r\n");
    s = LoadConfig(ini.path()).strip;
    CHECK(s.items[0].label_set);
    CHECK(s.items[1].label_set);  // 空值表示不显示，也算用户设置过
    ini.Write(L"[strip]\r\nlabel_memory = RAM\r\n");
    CHECK(!LoadConfig(ini.path()).strip.items[1].label_set);  // 两种语言的默认名称都是 RAM
    // items 里写了名称时，以 items 为准
    ini.Write(L"[strip]\r\nlabel_cpu = 处理器\r\nitems = cpu=核心, memory\r\n");
    s = LoadConfig(ini.path()).strip;
    CHECK(wcscmp(s.items[0].label, L"核心") == 0);
    CHECK(!s.items[1].label_set);
}
TEST_CASE("config: guid parse and format round-trip") {
    const auto g = pitot::config::ParseGuid(L" {6B29FC40-CA47-1067-B31D-00DD010662DA} ");
    CHECK(g.has_value());
    CHECK(g->Data1 == 0x6B29FC40ul);
    CHECK(g->Data2 == 0xCA47);
    CHECK(g->Data3 == 0x1067);
    CHECK(g->Data4[0] == 0xB3 && g->Data4[7] == 0xDA);
    wchar_t text[40];
    pitot::config::FormatGuid(*g, text, 40);
    CHECK(wcscmp(text, L"{6B29FC40-CA47-1067-B31D-00DD010662DA}") == 0);
    CHECK(pitot::config::ParseGuid(L"6b29fc40-ca47-1067-b31d-00dd010662da").has_value());
    CHECK(!pitot::config::ParseGuid(L""));
    CHECK(!pitot::config::ParseGuid(L"{6B29FC40-CA47-1067-B31D-00DD010662DX}"));
    CHECK(!pitot::config::ParseGuid(L"{6B29FC40CA47-1067-B31D-00DD010662DA0}"));
}

TEST_CASE("config: network section") {
    TempIni ini;
    ini.Write(L"[network]\r\nadapter = {6B29FC40-CA47-1067-B31D-00DD010662DA}\r\nshow_virtual = 0\r\nusage = 0\r\n"
              L"reset_day = 40\r\nping = 1\r\nping_target = example.com\r\n");
    const auto n = LoadConfig(ini.path()).network;
    CHECK(n.adapter.has_value());
    CHECK(!n.show_virtual);
    CHECK(!n.usage);
    CHECK(n.reset_day == pitot::config::kMaxResetDay);
    CHECK(n.ping);
    CHECK(wcscmp(n.ping_target, L"example.com") == 0);
    // 空 adapter 是“自动”，空目标用默认
    ini.Write(L"[network]\r\nadapter =\r\nping_target =\r\n");
    const auto d = LoadConfig(ini.path()).network;
    CHECK(!d.adapter);
    CHECK(wcscmp(d.ping_target, L"1.1.1.1") == 0);
}

TEST_CASE("config: adapter choice is written back in UTF-16") {
    TempIni ini;
    CHECK(WriteDefaultConfig(ini.path()));
    const auto g = pitot::config::ParseGuid(L"{6B29FC40-CA47-1067-B31D-00DD010662DA}");
    CHECK(pitot::config::WriteNetworkAdapter(ini.path(), g));
    CHECK(LoadConfig(ini.path()).network.adapter.has_value());
    CHECK(pitot::config::WriteNetworkAdapter(ini.path(), std::nullopt));
    CHECK(!LoadConfig(ini.path()).network.adapter);
    // UTF-8 文件不写，免得被按 ANSI 改写
    ini.Write(L"[network]\r\nadapter =\r\n", true);
    CHECK(!pitot::config::WriteNetworkAdapter(ini.path(), g));
}

TEST_CASE("config: intervals, flyout behavior and temperature thresholds") {
    TempIni ini;
    ini.Write(L"[general]\r\ninterval = 2\r\nsensor_interval = 99\r\n[flyout]\r\nauto_close = 0\r\nopen_panel = Network\r\n"
              L"[sensors]\r\nwarn = 70\r\nhot = 65\r\n");
    const Config c = LoadConfig(ini.path());
    CHECK(c.general.interval_ms == 2000);
    CHECK(c.general.sensor_interval_ms == 30000);
    CHECK(!c.flyout.auto_close);
    CHECK(c.flyout.open_panel == 4);
    CHECK(c.sensors.warn == 70);
    CHECK(c.sensors.hot == 70);  // 不低于警告阈值
    ini.Write(L"[flyout]\r\nopen_panel = last\r\n");
    CHECK(LoadConfig(ini.path()).flyout.open_panel == -1);
    // 小数秒取到 0.1 s；低于 1 s 截到 1 s
    ini.Write(L"[general]\r\ninterval = 1.5\r\nsensor_interval = 2.04\r\n");
    CHECK(LoadConfig(ini.path()).general.interval_ms == 1500);
    CHECK(LoadConfig(ini.path()).general.sensor_interval_ms == 2000);
    ini.Write(L"[general]\r\ninterval = 0.5\r\n");
    CHECK(LoadConfig(ini.path()).general.interval_ms == 1000);
    // 任务栏条单击行为：默认打开点中的面板
    CHECK(LoadConfig(ini.path()).strip.click_item);
    ini.Write(L"[strip]\r\nclick = flyout\r\n");
    CHECK(!LoadConfig(ini.path()).strip.click_item);
}

TEST_CASE("config: write value refuses non UTF-16 files") {
    TempIni ini;
    CHECK(WriteDefaultConfig(ini.path()));
    CHECK(pitot::config::WriteValue(ini.path(), L"sensors", L"warn", L"65"));
    CHECK(LoadConfig(ini.path()).sensors.warn == 65);
    ini.Write(L"[sensors]\r\nwarn = 60\r\n", true);
    CHECK(!pitot::config::WriteValue(ini.path(), L"sensors", L"warn", L"65"));
}

TEST_CASE("config: strip style, position, unit and taskbars") {
    using pitot::config::NetUnit;
    using pitot::config::StripPosition;
    using pitot::config::StripVariant;
    TempIni ini;
    ini.Write(L"[strip]\r\nstyle = B\r\nposition = right\r\nnet_unit = bits\r\nall_taskbars = 1\r\n");
    Config c = LoadConfig(ini.path());
    CHECK(c.strip.variant == StripVariant::Spark);
    CHECK(c.strip.position == StripPosition::Right);
    CHECK(c.strip.net_unit == NetUnit::Bits);
    CHECK(c.strip.all_taskbars);
    // 不认识的值回到默认
    ini.Write(L"[strip]\r\nstyle = z\r\nposition = top\r\nnet_unit = gb\r\n");
    c = LoadConfig(ini.path());
    CHECK(c.strip.variant == StripVariant::Stack);
    CHECK(c.strip.position == StripPosition::Left);
    CHECK(c.strip.net_unit == NetUnit::Auto);
    CHECK(!c.strip.all_taskbars);
    ini.Write(L"[strip]\r\nstyle = c\r\nnet_unit = MB\r\n");
    c = LoadConfig(ini.path());
    CHECK(c.strip.variant == StripVariant::Minimal);
    CHECK(c.strip.net_unit == NetUnit::Mega);
}

TEST_CASE("config: flyout tiles and history") {
    TempIni ini;
    ini.Write(L"[flyout]\r\ntiles = cpu, network, bogus\r\nhistory = 900\r\n");
    Config c = LoadConfig(ini.path());
    CHECK(c.flyout.tiles == ((1u << 0) | (1u << 4)));
    CHECK(c.flyout.history_seconds == 900);
    // 一个都不认识时恢复全部；时长截到 60–3600
    ini.Write(L"[flyout]\r\ntiles = nothing\r\nhistory = 99999\r\n");
    c = LoadConfig(ini.path());
    CHECK(c.flyout.tiles == 0x3F);
    CHECK(c.flyout.history_seconds == 3600);
    wchar_t text[96];
    pitot::config::FormatTiles(static_cast<uint8_t>((1u << 1) | (1u << 5)), text, 96);
    CHECK(wcscmp(text, L"gpu, sensors") == 0);
    CHECK(pitot::config::ParseTiles(text) == ((1u << 1) | (1u << 5)));
}

TEST_CASE("config: temperature unit, notification and sensor sections") {
    TempIni ini;
    ini.Write(L"[sensors]\r\nunit = F\r\nnotify = 1\r\n\r\n[sensor.NCT6687D.0x104]\r\nvisible = 1\r\nalias = 供电\r\n"
              L"\r\n[sensor.amd.ccd2]\r\nvisible = 0\r\n\r\n[sensorx]\r\nvisible = 0\r\n");
    const Config c = LoadConfig(ini.path());
    CHECK(c.sensors.fahrenheit);
    CHECK(c.sensors.notify);
    CHECK(c.sensors.setting_count == 2);
    // 键不分大小写
    CHECK(wcscmp(pitot::config::SensorName(c.sensors, L"nct6687d.0x104", L"默认"), L"供电") == 0);
    CHECK(wcscmp(pitot::config::SensorName(c.sensors, L"amd.ccd2", L"CCD2"), L"CCD2") == 0);
    CHECK(!pitot::config::SensorVisible(c.sensors, L"amd.ccd2"));
    CHECK(pitot::config::SensorVisible(c.sensors, L"amd.tctl"));
}

TEST_CASE("config: writing a sensor back to defaults removes its section") {
    TempIni ini;
    ini.Write(L"[sensors]\r\nwarn = 60\r\n");
    CHECK(pitot::config::WriteSensorSetting(ini.path(), L"nvme.0", false, L"系统盘"));
    Config c = LoadConfig(ini.path());
    CHECK(c.sensors.setting_count == 1);
    CHECK(!pitot::config::SensorVisible(c.sensors, L"nvme.0"));
    CHECK(pitot::config::WriteSensorSetting(ini.path(), L"nvme.0", true, L""));
    c = LoadConfig(ini.path());
    CHECK(c.sensors.setting_count == 0);
}
