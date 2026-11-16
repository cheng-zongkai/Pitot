#include "sampler/perf.h"

#include <pdhmsg.h>

#include <algorithm>
#include <cwchar>

#include "common/log.h"
#include "sampler/cpu_info.h"

namespace pitot::sampler {
namespace {

bool Valid(DWORD cstatus) { return cstatus == PDH_CSTATUS_VALID_DATA || cstatus == PDH_CSTATUS_NEW_DATA; }

}  // namespace

bool PerfSampler::Init() {
    PDH_HQUERY query = nullptr;
    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query);
    if (status != ERROR_SUCCESS) {
        log::Write(L"perf: PdhOpenQuery failed 0x%08lX", static_cast<unsigned long>(status));
        return false;
    }
    query_.reset(query);

    // 用英文计数器名，中文系统上本地化名称不同
    status = PdhAddEnglishCounterW(query, L"\\Processor Information(*)\\% Processor Utility", 0, &utility_);
    if (status != ERROR_SUCCESS) {
        log::Write(L"perf: add utility counter failed 0x%08lX", static_cast<unsigned long>(status));
        query_.reset();
        return false;
    }
    return true;
}

void PerfSampler::Sample(Snapshot& s) {
    if (!query_ || PdhCollectQueryData(query_.get()) != ERROR_SUCCESS) {
        return;
    }
    // 速率类计数器要两次采集才能算出值
    if (!primed_) {
        primed_ = true;
        return;
    }
    SampleUtility(s);
}

void PerfSampler::SampleUtility(Snapshot& s) {
    DWORD size = static_cast<DWORD>(items_.size());
    DWORD count = 0;
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(items_.data());
    PDH_STATUS status =
        PdhGetFormattedCounterArrayW(utility_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items);
    if (status == static_cast<PDH_STATUS>(PDH_MORE_DATA)) {
        // 只在第一次（或处理器数变化时）分配；多留一些余量
        items_.resize(size + size / 4);
        size = static_cast<DWORD>(items_.size());
        items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(items_.data());
        status = PdhGetFormattedCounterArrayW(utility_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items);
    }
    if (status != ERROR_SUCCESS) {
        return;
    }

    int cpu_count = 0;
    for (DWORD i = 0; i < count; ++i) {
        if (!Valid(items[i].FmtValue.CStatus)) {
            continue;
        }
        // 睿频时会超过 100%，和任务管理器一样截断
        const double value = std::clamp(items[i].FmtValue.doubleValue, 0.0, 100.0);
        if (wcscmp(items[i].szName, L"_Total") == 0) {
            s.cpu_percent = value;
            continue;
        }
        const int index = ParseProcessorInstance(items[i].szName);
        if (index >= 0 && index < kMaxCpus) {
            s.cpu_core_percent[static_cast<size_t>(index)] = static_cast<float>(value);
            cpu_count = std::max(cpu_count, index + 1);
        }
    }
    s.cpu_count = cpu_count;
}

}  // namespace pitot::sampler
