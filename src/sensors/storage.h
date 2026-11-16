#pragma once

#include <windows.h>

#include <pdh.h>

#include <array>
#include <optional>
#include <vector>

#include "common/unique_handle.h"
#include "sampler/snapshot.h"

namespace pitot::sensors {

// 存储（D-033）：
// - 读取、写入、活动时间：PDH \PhysicalDisk(*)，每秒；
// - 型号、卷与盘的对应、容量：IOCTL_STORAGE_QUERY_PROPERTY、IOCTL_STORAGE_GET_DEVICE_NUMBER、
//   GetDiskFreeSpaceExW，30 秒重扫一次（顺带发现新插的盘）；
// - 温度：StorageDeviceTemperatureProperty，10 秒一次，只读 NVMe（查询温度可能把休眠的机械盘唤醒）。
// 全部只读。稳态不分配堆内存：PDH 输出缓冲第一次按需分配后复用。
class StorageSensor {
public:
    bool Init();
    void Sample(ULONGLONG now_ms, sampler::StorageReadings& out);

private:
    struct QueryTraits {
        using pointer = PDH_HQUERY;
        static pointer invalid() noexcept { return nullptr; }
        static void close(pointer q) noexcept { PdhCloseQuery(q); }
    };
    struct Disk {
        uint32_t number = 0;
        wchar_t model[48]{};
        wchar_t letters[24]{};
        bool nvme = false;
        std::optional<uint64_t> total_bytes;
        std::optional<uint64_t> free_bytes;
        std::optional<float> temperature;
    };
    struct Rates {
        std::optional<double> read;
        std::optional<double> write;
        std::optional<double> active;
    };

    void Rescan();
    void ReadTemperatures();
    void CollectRates(bool collected);
    Disk* Find(uint32_t number);

    unique_handle_t<QueryTraits> query_;
    PDH_HCOUNTER read_ = nullptr;
    PDH_HCOUNTER write_ = nullptr;
    PDH_HCOUNTER idle_ = nullptr;
    std::vector<BYTE> buffer_;
    bool primed_ = false;

    std::array<Disk, sampler::kMaxDisks> disks_{};
    std::array<Rates, sampler::kMaxDisks> rates_{};
    int count_ = 0;
    ULONGLONG last_rescan_ = 0;
    ULONGLONG last_temperature_ = 0;
    bool scanned_ = false;
    bool temperature_logged_ = false;
};

}  // namespace pitot::sensors
