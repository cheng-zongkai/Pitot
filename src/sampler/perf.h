#pragma once

#include <windows.h>

#include <pdh.h>

#include <vector>

#include "common/unique_handle.h"
#include "sampler/snapshot.h"

namespace pitot::sampler {

// PDH 性能计数器（D-012、D-027）：“% Processor Utility”所有实例，_Total 是总占用，
// “组,编号”是每个逻辑处理器，与任务管理器同口径。
class PerfSampler {
public:
    bool Init();
    // 每个周期调用一次。第一次只建立基准，不填值
    void Sample(Snapshot& s);

private:
    struct QueryTraits {
        using pointer = PDH_HQUERY;
        static pointer invalid() noexcept { return nullptr; }
        static void close(pointer q) noexcept { PdhCloseQuery(q); }
    };

    void SampleUtility(Snapshot& s);

    unique_handle_t<QueryTraits> query_;
    PDH_HCOUNTER utility_ = nullptr;
    // PdhGetFormattedCounterArray 的输出缓冲区，第一次按需要的大小分配，之后复用
    std::vector<BYTE> items_;
    bool primed_ = false;
};

}  // namespace pitot::sampler
