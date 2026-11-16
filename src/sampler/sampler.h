#pragma once

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <thread>

#include "common/unique_handle.h"
#include "config/config.h"
#include "sampler/icon_store.h"
#include "sampler/snapshot.h"
#include "sampler/triple_buffer.h"

namespace pitot::sampler {

// 采样线程。每个周期采一轮，发布到三缓冲，再给通知窗口发 msg::kSnapshotReady。
class Sampler {
public:
    Sampler() = default;
    ~Sampler() { Stop(); }
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    // 演示模式（D-083）：必须在 Start 之前设置
    void SetDemo(bool on) noexcept { demo_ = on; }
    bool Start(HWND notify);
    void Stop();

    // 浮窗打开时开启详细指标（D-027）。开启时立即采一轮，不等下一个周期
    void SetDetail(bool on);
    // 进程友好名称的语言跟随界面语言
    void SetEnglishNames(bool english) { english_names_.store(english); }
    // 进程列表行数（config.ini [flyout] process_rows）
    void SetProcessRows(int rows) { process_rows_.store(rows); }
    // 刷新间隔，毫秒（[general] interval / sensor_interval）
    void SetIntervals(int base_ms, int sensor_ms) {
        interval_ms_.store(std::max(base_ms, kMinIntervalMs));
        sensor_interval_ms_.store(std::max(sensor_ms, kMinIntervalMs));
    }
    static constexpr int kMinIntervalMs = 1000;
    // 浮窗当前面板（FlyoutWindow::Panel 的值）。切到 GPU 面板时立即采一轮
    // [network] 设置（网卡选择、延迟、流量统计）。UI 线程调用，采样线程下一轮取走
    void SetNetwork(const config::NetworkConfig& network);
    void SetPanel(int panel);
    static constexpr int kPanelGpu = 1;
    static constexpr int kPanelDisk = 3;
    static constexpr int kPanelNetwork = 4;

    // 只在 UI 线程调用：有新快照时返回 true，之后 Latest() 指向它
    bool Acquire() noexcept { return buffer_.Acquire(); }
    const Snapshot& Latest() const noexcept { return buffer_.ReadBuffer(); }
    // 进程图标，UI 线程按快照里的槽位和版本号读（不加锁，见 IconStore）
    const IconStore& icons() const noexcept { return icons_; }

private:
    void Run() noexcept;
    void RunDemo() noexcept;

    HWND notify_ = nullptr;
    bool demo_ = false;
    unique_handle stop_event_;
    unique_handle wake_event_;
    std::atomic<bool> detail_{false};
    std::atomic<bool> english_names_{false};
    std::atomic<int> process_rows_{8};
    std::atomic<int> interval_ms_{1000};
    std::atomic<int> sensor_interval_ms_{2000};
    std::atomic<int> panel_{-1};
    // 网络设置由 UI 线程写、采样线程读；只在设置变化时加锁复制一次
    SRWLOCK network_lock_ = SRWLOCK_INIT;
    config::NetworkConfig network_;
    std::atomic<bool> network_dirty_{true};
    std::thread thread_;
    TripleBuffer<Snapshot> buffer_;
    IconStore icons_;
};

}  // namespace pitot::sampler
