#pragma once

#include "sampler/demo_mode.h"
#include "sampler/icon_store.h"
#include "sampler/snapshot.h"

// 演示模式（Pitot.exe --demo）：不碰硬件、驱动和用户的进程，用一台虚构机器的假数据代替采样结果，
// 给文档截图用。数值是时间的确定函数：同一时刻总是同一个值，曲线平滑，隔一会儿有一次小的突发。

namespace pitot::sampler {

// 除进程列表以外的全部指标。t 是演示时间（秒），可以为负（预填趋势线用）。不分配内存
void FillDemoMetrics(Snapshot& s, double t, bool english);

// 进程列表（浮窗打开时才有的那部分）。图标从系统自带的程序里取，只在 Init 里取一次
class DemoProcesses {
public:
    void Init(IconStore& icons);
    void Fill(Snapshot& s, double t, IconStore& icons);

private:
    static constexpr int kAppCount = 10;
    int icon_[kAppCount]{};
    uint32_t icon_version_[kAppCount]{};
};

}  // namespace pitot::sampler
