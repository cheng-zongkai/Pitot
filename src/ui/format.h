#pragma once

#include <cstddef>
#include <optional>

#include "config/config.h"

// 数值格式化。纯函数，不依赖窗口，有单元测试。输出总会以 0 结尾，缓冲不够时截断。

namespace pitot::ui {

// "23%"；超出 0–100 先截断；不可用时为占位符 "—"
void FormatPercent(std::optional<double> percent, wchar_t* out, size_t size);

// 速率，1024 进制，最多 3 位数字，最小单位 KB/s（D-036）："0 KB/s"（不到 1 KB/s）、"2.4 KB/s"、"180 KB/s"、
// "1.0 MB/s"；不可用时为 "—"
void FormatRate(std::optional<double> bytes_per_sec, wchar_t* out, size_t size);

// 浮窗网络速率：带收发箭头；不到 1 KB/s 时显示整数 B/s，较高速率沿用 FormatRate（D-051）
enum class NetworkDirection { Download, Upload };
void FormatNetworkRate(std::optional<double> bytes_per_sec, NetworkDirection direction, wchar_t* out, size_t size);

// 和 FormatRate 同样的取值，但数字和单位分开："180" + "KB/s"。任务栏条用它把数字放在固定宽度的列里，
// 单位不随位数左右移动。不可用时数字为 "—"、单位为空串。unit 指向静态字符串
void FormatRateParts(std::optional<double> bytes_per_sec, wchar_t* number, size_t size, const wchar_t*& unit);

template <size_t N>
void FormatRateParts(std::optional<double> bytes_per_sec, wchar_t (&number)[N], const wchar_t*& unit) {
    FormatRateParts(bytes_per_sec, number, N, unit);
}

// 所有可能的速率单位，量列宽用。不用 B/s：不到 1 KB/s 记作 0 KB/s，单位宽度一致（D-036）
inline constexpr const wchar_t* kAllRateUnits[] = {L"KB/s", L"MB/s", L"GB/s", L"TB/s"};

// 任务栏条网速按 [strip] net_unit 的单位（D-073）：Auto 同 FormatRateParts；Bits 是 1000 进制的
// Kbps / Mbps / Gbps（最小 Kbps，不到 1 Kbps 记 0）；Kilo 固定 KB/s，Mega 固定 MB/s
void FormatNetRateParts(std::optional<double> bytes_per_sec, config::NetUnit unit, wchar_t* number, size_t size,
                        const wchar_t*& unit_text);

template <size_t N>
void FormatNetRateParts(std::optional<double> bytes_per_sec, config::NetUnit unit, wchar_t (&number)[N],
                        const wchar_t*& unit_text) {
    FormatNetRateParts(bytes_per_sec, unit, number, N, unit_text);
}

// 比特单位，量列宽用
inline constexpr const wchar_t* kAllBitRateUnits[] = {L"Kbps", L"Mbps", L"Gbps", L"Tbps"};

// 紧凑网速，任务栏条样式 B / C 用：Auto “2.4M”，Bits “19Mb”，Kilo “2400K”，Mega “2.40M”
void FormatNetRateShort(std::optional<double> bytes_per_sec, config::NetUnit unit, wchar_t* out, size_t size);

template <size_t N>
void FormatNetRateShort(std::optional<double> bytes_per_sec, config::NetUnit unit, wchar_t (&out)[N]) {
    FormatNetRateShort(bytes_per_sec, unit, out, N);
}

// 悬停提示用的详细版本：“23.4%”
void FormatPercentDetailed(std::optional<double> percent, wchar_t* out, size_t size);

// 悬停提示用的详细版本：3 位有效数字，“6.71 KB/s”、“18.0 KB/s”、“180 KB/s”；不到 1 KB/s 时“0.50 KB/s”
void FormatRateDetailed(std::optional<double> bytes_per_sec, wchar_t* out, size_t size);

// 字节数，1024 进制，一位小数：“15.9 GB”
void FormatBytes(std::optional<double> bytes, wchar_t* out, size_t size);

// 字节数，3 位有效数字：“3.84 GB”“812 MB”“53.2 MB”。Top 进程列表用
void FormatBytesDetailed(std::optional<double> bytes, wchar_t* out, size_t size);

template <size_t N>
void FormatBytesDetailed(std::optional<double> bytes, wchar_t (&out)[N]) {
    FormatBytesDetailed(bytes, out, N);
}

// 紧凑速率，磁贴角标用：“180K”“2.4M”“512B”；不可用时为 "—"
void FormatRateShort(std::optional<double> bytes_per_sec, wchar_t* out, size_t size);

template <size_t N>
void FormatRateShort(std::optional<double> bytes_per_sec, wchar_t (&out)[N]) {
    FormatRateShort(bytes_per_sec, out, N);
}

// 温度单位（D-071）。全进程一份，只在 UI 线程设置；数值本身始终按 ℃ 传进来
void SetFahrenheit(bool fahrenheit);
bool UsesFahrenheit();
double DisplayTemperature(double celsius);
// “54.8 °C”“131 °F”。decimals 是小数位数
void FormatTemperature(double celsius, int decimals, wchar_t* out, size_t size);

template <size_t N>
void FormatTemperature(double celsius, int decimals, wchar_t (&out)[N]) {
    FormatTemperature(celsius, decimals, out, N);
}

// 运行时长：“3 天 4 小时”“4 小时 12 分”“12 分钟”
void FormatUptime(unsigned long long milliseconds, wchar_t* out, size_t size);

template <size_t N>
void FormatUptime(unsigned long long milliseconds, wchar_t (&out)[N]) {
    FormatUptime(milliseconds, out, N);
}

template <size_t N>
void FormatPercent(std::optional<double> percent, wchar_t (&out)[N]) {
    FormatPercent(percent, out, N);
}

template <size_t N>
void FormatRate(std::optional<double> bytes_per_sec, wchar_t (&out)[N]) {
    FormatRate(bytes_per_sec, out, N);
}

template <size_t N>
void FormatNetworkRate(std::optional<double> bytes_per_sec, NetworkDirection direction, wchar_t (&out)[N]) {
    FormatNetworkRate(bytes_per_sec, direction, out, N);
}

template <size_t N>
void FormatPercentDetailed(std::optional<double> percent, wchar_t (&out)[N]) {
    FormatPercentDetailed(percent, out, N);
}

template <size_t N>
void FormatRateDetailed(std::optional<double> bytes_per_sec, wchar_t (&out)[N]) {
    FormatRateDetailed(bytes_per_sec, out, N);
}

template <size_t N>
void FormatBytes(std::optional<double> bytes, wchar_t (&out)[N]) {
    FormatBytes(bytes, out, N);
}

}  // namespace pitot::ui
