#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace pitot::ui {

// 定长环形缓冲，存最近 N 个采样（趋势线用）。不分配堆内存。
// 存 float，不可用的采样记为 NaN：最长 1 小时（D-076）时每条 3600 个点，用 optional<double> 要多占三倍
template <size_t N>
class History {
public:
    // 不可用的采样记为空，画趋势线时跳过
    void Push(std::optional<double> value) noexcept {
        values_[next_] = value && std::isfinite(*value) ? static_cast<float>(*value)
                                                        : std::numeric_limits<float>::quiet_NaN();
        next_ = (next_ + 1) % N;
        if (count_ < N) {
            ++count_;
        }
    }

    [[nodiscard]] size_t size() const noexcept { return count_; }
    static constexpr size_t capacity() noexcept { return N; }

    // 第 i 个采样，0 是最老的
    [[nodiscard]] std::optional<double> at(size_t i) const noexcept {
        if (i >= count_) {
            return std::nullopt;
        }
        const float v = values_[(next_ + N - count_ + i) % N];
        return std::isnan(v) ? std::nullopt : std::optional<double>(v);
    }

private:
    std::array<float, N> values_{};
    size_t next_ = 0;
    size_t count_ = 0;
};

// 把最近 window 个采样压成 points 个点（每个点是一段采样的平均），从老到新写进 out，最新的在最后。
// 还没记满 window 时前面的点是 NaN，曲线从右往左长；一段里全不可用时也是 NaN。返回实际的点数
template <size_t N>
int Downsample(const History<N>& history, size_t window, float* out, int points) {
    window = std::clamp<size_t>(window, 2, N);
    points = std::clamp(points, 2, static_cast<int>(window));
    const auto first = static_cast<long long>(history.size()) - static_cast<long long>(window);
    for (int p = 0; p < points; ++p) {
        const auto begin = first + static_cast<long long>(window) * p / points;
        const auto end = first + static_cast<long long>(window) * (p + 1) / points;
        double sum = 0;
        int n = 0;
        for (long long i = std::max(0LL, begin); i < end; ++i) {
            if (const auto v = history.at(static_cast<size_t>(i))) {
                sum += *v;
                ++n;
            }
        }
        out[p] = n > 0 ? static_cast<float>(sum / n) : std::numeric_limits<float>::quiet_NaN();
    }
    return points;
}

// 趋势线最多记多少个采样：1 秒一轮时正好 1 小时（D-076）
inline constexpr size_t kHistoryCapacity = 3600;

// 趋势：浮窗磁贴和任务栏条样式 B 的迷你曲线用。浮窗关着时也在记录，打开就有完整曲线
struct Trends {
    History<kHistoryCapacity> cpu;
    History<kHistoryCapacity> memory;
    History<kHistoryCapacity> rx;
    History<kHistoryCapacity> gpu;          // 3D 利用率
    History<kHistoryCapacity> storage;      // 所有盘读写之和
    History<kHistoryCapacity> temperature;  // 各通道最高温度（隐藏的不算）
    // 以下只给任务栏条样式 B 用，和任务栏条显示项的来源一一对应
    History<kHistoryCapacity> disk_active;
    History<kHistoryCapacity> temp_cpu;
    History<kHistoryCapacity> temp_gpu;
    History<kHistoryCapacity> temp_storage;
    History<kHistoryCapacity> temp_board;
    // 曲线覆盖最近多少个采样：[flyout] history 秒数 ÷ 刷新间隔
    size_t window = 60;
};

// 设定的时长（秒）和刷新间隔（毫秒）→ 曲线要画多少个采样
inline size_t HistoryWindow(int seconds, int interval_ms) {
    const long long samples = static_cast<long long>(seconds) * 1000 / std::max(interval_ms, 100);
    return std::clamp<size_t>(static_cast<size_t>(std::max(2LL, samples)), 2, kHistoryCapacity);
}

}  // namespace pitot::ui
