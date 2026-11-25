#include "sensors/storage.h"

#include <winioctl.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cwchar>

#include "common/log.h"
#include "sensors/parse.h"

namespace pitot::sensors {
namespace {

constexpr ULONGLONG kRescanMs = 30'000;      // 容量、型号、卷（见 docs/subsystems/storage.md）
constexpr ULONGLONG kTemperatureMs = 10'000;  // NVMe 温度（见 docs/subsystems/storage.md）
constexpr uint32_t kMaxDriveNumber = 16;      // 只看 PhysicalDrive0–15
// 温度读数超出这个范围当作无效
constexpr int kMinCelsius = -40;
constexpr int kMaxCelsius = 150;

bool Valid(DWORD cstatus) { return cstatus == PDH_CSTATUS_VALID_DATA || cstatus == PDH_CSTATUS_NEW_DATA; }

unique_file_handle OpenDrive(uint32_t number) {
    wchar_t path[32];
    _snwprintf_s(path, _TRUNCATE, L"\\\\.\\PhysicalDrive%u", number);
    // 访问权限为 0：只做查询类 IOCTL，不读写盘上的数据
    return unique_file_handle(
        CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
}

void Append(wchar_t* list, size_t size, wchar_t letter) {
    const size_t n = wcslen(list);
    if (n + 4 > size) {
        return;
    }
    if (n > 0) {
        list[n] = L' ';
    }
    const size_t at = n > 0 ? n + 1 : 0;
    list[at] = letter;
    list[at + 1] = L':';
    list[at + 2] = L'\0';
}

}  // namespace

bool StorageSensor::Init() {
    PDH_HQUERY query = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
        log::Write(L"storage: PdhOpenQuery failed");
        return false;
    }
    query_.reset(query);
    // 英文计数器名，中文系统上也能用
    const PDH_STATUS a = PdhAddEnglishCounterW(query, L"\\PhysicalDisk(*)\\Disk Read Bytes/sec", 0, &read_);
    const PDH_STATUS b = PdhAddEnglishCounterW(query, L"\\PhysicalDisk(*)\\Disk Write Bytes/sec", 0, &write_);
    const PDH_STATUS c = PdhAddEnglishCounterW(query, L"\\PhysicalDisk(*)\\% Idle Time", 0, &idle_);
    if (a != ERROR_SUCCESS || b != ERROR_SUCCESS || c != ERROR_SUCCESS) {
        log::Write(L"storage: add counters failed 0x%08lX 0x%08lX 0x%08lX", static_cast<unsigned long>(a),
                   static_cast<unsigned long>(b), static_cast<unsigned long>(c));
        query_.reset();
        read_ = write_ = idle_ = nullptr;
    }
    return true;
}

StorageSensor::Disk* StorageSensor::Find(uint32_t number) {
    for (int i = 0; i < count_; ++i) {
        if (disks_[static_cast<size_t>(i)].number == number) {
            return &disks_[static_cast<size_t>(i)];
        }
    }
    return nullptr;
}

void StorageSensor::Rescan() {
    std::array<Disk, sampler::kMaxDisks> found{};
    int n = 0;
    for (uint32_t number = 0; number < kMaxDriveNumber && n < sampler::kMaxDisks; ++number) {
        const unique_file_handle drive = OpenDrive(number);
        if (!drive) {
            continue;
        }
        Disk& d = found[static_cast<size_t>(n++)];
        d.number = number;
        STORAGE_PROPERTY_QUERY query{StorageDeviceProperty, PropertyStandardQuery, {0}};
        alignas(8) BYTE buffer[1024]{};
        DWORD bytes = 0;
        if (DeviceIoControl(drive.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer),
                            &bytes, nullptr) &&
            bytes >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer);
            if (desc->ProductIdOffset && desc->ProductIdOffset < bytes) {
                buffer[sizeof(buffer) - 1] = 0;
                CleanModelName(reinterpret_cast<const char*>(buffer + desc->ProductIdOffset), d.model,
                               std::size(d.model));
            }
            d.nvme = desc->BusType == BusTypeNvme;
        }
        if (d.model[0] == L'\0') {
            _snwprintf_s(d.model, _TRUNCATE, L"PhysicalDrive%u", number);
        }
        // 温度跨重扫保留，免得每 30 秒闪一下“—”
        if (const Disk* old = Find(number)) {
            d.temperature = old->temperature;
        }
    }

    // 卷 → 盘：只看本地固定卷；跨多块盘的动态卷拿不到单一盘号，跳过
    wchar_t drives[128];
    const DWORD length = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);
    if (length > 0 && length < std::size(drives)) {
        for (const wchar_t* root = drives; *root; root += wcslen(root) + 1) {
            if (GetDriveTypeW(root) != DRIVE_FIXED) {
                continue;
            }
            wchar_t volume[16];
            _snwprintf_s(volume, _TRUNCATE, L"\\\\.\\%c:", root[0]);
            const unique_file_handle handle(
                CreateFileW(volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
            STORAGE_DEVICE_NUMBER device{};
            DWORD bytes = 0;
            if (!handle || !DeviceIoControl(handle.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &device,
                                            sizeof(device), &bytes, nullptr)) {
                continue;
            }
            for (int i = 0; i < n; ++i) {
                Disk& d = found[static_cast<size_t>(i)];
                if (d.number != device.DeviceNumber) {
                    continue;
                }
                Append(d.letters, std::size(d.letters), root[0]);
                ULARGE_INTEGER total{};
                ULARGE_INTEGER free{};
                if (GetDiskFreeSpaceExW(root, nullptr, &total, &free)) {
                    d.total_bytes = d.total_bytes.value_or(0) + total.QuadPart;
                    d.free_bytes = d.free_bytes.value_or(0) + free.QuadPart;
                }
                break;
            }
        }
    }
    disks_ = found;
    count_ = n;
}

void StorageSensor::ReadTemperatures() {
    for (int i = 0; i < count_; ++i) {
        Disk& d = disks_[static_cast<size_t>(i)];
        if (!d.nvme) {
            continue;
        }
        const unique_file_handle drive = OpenDrive(d.number);
        STORAGE_PROPERTY_QUERY query{StorageDeviceTemperatureProperty, PropertyStandardQuery, {0}};
        alignas(8) BYTE buffer[512]{};
        DWORD bytes = 0;
        if (!drive || !DeviceIoControl(drive.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer,
                                       sizeof(buffer), &bytes, nullptr) ||
            bytes < sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR)) {
            if (!temperature_logged_) {
                temperature_logged_ = true;
                log::Write(L"storage: temperature of disk %u unavailable %lu", d.number, GetLastError());
            }
            d.temperature.reset();
            continue;
        }
        // 第 0 项是整盘的综合温度（NVMe Composite Temperature）
        const auto* desc = reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(buffer);
        const int celsius = desc->InfoCount > 0 ? desc->TemperatureInfo[0].Temperature : 0;
        if (desc->InfoCount > 0 && celsius > kMinCelsius && celsius < kMaxCelsius && celsius != 0) {
            d.temperature = static_cast<float>(celsius);
        } else {
            d.temperature.reset();
        }
    }
}

void StorageSensor::CollectRates(bool collected) {
    for (auto& r : rates_) {
        r = {};
    }
    if (!collected) {
        return;
    }
    if (!primed_) {
        primed_ = true;  // 速率要两次采集才有值
        return;
    }
    const PDH_HCOUNTER counters[] = {read_, write_, idle_};
    for (int c = 0; c < 3; ++c) {
        DWORD size = static_cast<DWORD>(buffer_.size());
        DWORD count = 0;
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer_.data());
        PDH_STATUS status = PdhGetFormattedCounterArrayW(counters[c], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
                                                         items);
        if (status == static_cast<PDH_STATUS>(PDH_MORE_DATA)) {
            // 只在第一次和盘变多时分配
            buffer_.resize(size + size / 2);
            size = static_cast<DWORD>(buffer_.size());
            items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer_.data());
            status = PdhGetFormattedCounterArrayW(counters[c], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items);
        }
        if (status != ERROR_SUCCESS) {
            continue;
        }
        for (DWORD i = 0; i < count; ++i) {
            const auto number = ParseDiskInstance(items[i].szName);
            if (!number || !Valid(items[i].FmtValue.CStatus)) {
                continue;
            }
            for (int k = 0; k < count_; ++k) {
                if (disks_[static_cast<size_t>(k)].number != *number) {
                    continue;
                }
                Rates& r = rates_[static_cast<size_t>(k)];
                const double v = std::max(0.0, items[i].FmtValue.doubleValue);
                if (c == 0) {
                    r.read = v;
                } else if (c == 1) {
                    r.write = v;
                } else {
                    r.active = std::clamp(100.0 - v, 0.0, 100.0);
                }
                break;
            }
        }
    }
}

void StorageSensor::Sample(ULONGLONG now_ms, sampler::StorageReadings& out) {
    // 先记下计数器，再做可能很慢的重新扫描和温度查询：速率区间的终点要和进程列表的 ETW 分界
    // 在同一时刻（D-063）。格式化放在后面，按扫描后的盘对应
    const bool collected = query_ && PdhCollectQueryData(query_.get()) == ERROR_SUCCESS;
    if (!scanned_ || now_ms - last_rescan_ >= kRescanMs) {
        Rescan();
        if (!scanned_) {
            for (int i = 0; i < count_; ++i) {
                const Disk& d = disks_[static_cast<size_t>(i)];
                log::Write(L"storage: disk %u [%s] %s nvme=%d", d.number, d.model, d.letters, d.nvme);
            }
        }
        scanned_ = true;
        last_rescan_ = now_ms;
    }
    if (last_temperature_ == 0 || now_ms - last_temperature_ >= kTemperatureMs) {
        ReadTemperatures();
        last_temperature_ = now_ms;
    }
    CollectRates(collected);

    out.count = count_;
    for (int i = 0; i < count_; ++i) {
        const Disk& d = disks_[static_cast<size_t>(i)];
        const Rates& r = rates_[static_cast<size_t>(i)];
        sampler::DiskReadings& o = out.disks[static_cast<size_t>(i)];
        o.number = d.number;
        wcscpy_s(o.model, d.model);
        wcscpy_s(o.letters, d.letters);
        o.read_bytes_per_sec = r.read;
        o.write_bytes_per_sec = r.write;
        o.active_percent = r.active;
        o.total_bytes = d.total_bytes;
        o.free_bytes = d.free_bytes;
        o.temperature = d.temperature;
    }
}

}  // namespace pitot::sensors
