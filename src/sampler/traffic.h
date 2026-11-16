#pragma once

#include <guiddef.h>

#include <cstddef>
#include <cstdint>

// 今日 / 本月流量（M7，D-041）。按天、按网卡累计收发字节数；GUID 全 0 的键是“自动”（全部物理网卡之和）。
// 定长记录表，稳态不分配堆内存；落盘成一行一条的文本，程序自己读写。

namespace pitot::sampler {

// 日期用 yyyymmdd 的整数，便于比较和存盘
uint32_t MakeDate(int year, int month, int day) noexcept;
// 往前或往后挪 days 天（公历）
uint32_t AddDays(uint32_t date, int days) noexcept;
// 本统计周期的第一天：每月 reset_day 号开始（1–28）
uint32_t PeriodStart(uint32_t date, int reset_day) noexcept;

// “自动”（全部物理网卡之和）用的键
inline constexpr GUID kAutoTrafficKey{};

struct TrafficTotals {
    uint64_t rx = 0;
    uint64_t tx = 0;
};

class TrafficBook {
public:
    // 4 块网卡 + “自动” × 62 天约 310 条，留出余量
    static constexpr int kMaxRecords = 512;
    // 只保留最近这么多天：够算任意重置日的本月
    static constexpr int kKeepDays = 62;

    // 累加到 date 当天、key 这块网卡。表满时先删最老的日期，仍然满就丢掉这次（返回 false）
    bool Add(uint32_t date, const GUID& key, uint64_t rx, uint64_t tx) noexcept;
    // [from, to] 两天之间（含）的合计
    [[nodiscard]] TrafficTotals Sum(const GUID& key, uint32_t from, uint32_t to) const noexcept;
    // 删掉早于 oldest 的记录
    void Prune(uint32_t oldest) noexcept;

    [[nodiscard]] int count() const noexcept { return count_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void ClearDirty() noexcept { dirty_ = false; }

    // 文本格式：每行 “yyyymmdd {GUID} 收 发”，“自动”的 GUID 写成 auto。认不出的行跳过
    void Parse(const char* text, size_t length) noexcept;
    // 逐行输出（不含换行），返回 false 时停止
    template <typename F>
    bool ForEachLine(F&& emit) const {
        char line[96];
        for (int i = 0; i < count_; ++i) {
            FormatLine(records_[i], line, sizeof(line));
            if (!emit(line)) {
                return false;
            }
        }
        return true;
    }

    // %LOCALAPPDATA%\Pitot 下的数据文件。Load 失败（文件不存在、读错）时表为空；
    // Save 先写临时文件再替换，写到一半断电不会把旧数据弄坏
    bool Load(const wchar_t* path) noexcept;
    bool Save(const wchar_t* path) noexcept;

private:
    struct Record {
        uint32_t date = 0;
        GUID key{};
        uint64_t rx = 0;
        uint64_t tx = 0;
    };
    static void FormatLine(const Record& r, char* out, size_t size) noexcept;
    void RemoveOldestDate() noexcept;

    Record records_[kMaxRecords]{};
    int count_ = 0;
    bool dirty_ = false;
};

}  // namespace pitot::sampler
