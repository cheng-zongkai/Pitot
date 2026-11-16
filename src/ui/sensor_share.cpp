#include "ui/sensor_share.h"

#include <algorithm>
#include <cwchar>

#include "common/log.h"
#include "common/names.h"

namespace pitot::ui {

bool SensorShareWriter::Open() {
    if (view_) {
        return true;
    }
    mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedSensors),
                                      names::kSensorShareMapping));
    if (!mapping_) {
        log::Write(L"sensors: CreateFileMapping failed %lu", GetLastError());
        return false;
    }
    view_.reset(MapViewOfFile(mapping_.get(), FILE_MAP_WRITE, 0, 0, sizeof(SharedSensors)));
    if (!view_) {
        log::Write(L"sensors: MapViewOfFile failed %lu", GetLastError());
        mapping_.reset();
        return false;
    }
    return true;
}

void SensorShareWriter::Publish(const SensorList& list, int count) {
    if (!view_) {
        return;
    }
    auto* shared = static_cast<SharedSensors*>(view_.get());
    InterlockedIncrement(&shared->sequence);
    const int n = std::clamp(count, 0, kMaxSensorItems);
    for (int i = 0; i < n; ++i) {
        const SensorItem& item = list[static_cast<size_t>(i)];
        SharedSensor& out = shared->items[i];
        wcscpy_s(out.key, item.key);
        wcscpy_s(out.name, item.name);
        out.fan = item.fan ? 1 : 0;
        out.has_value = item.value ? 1 : 0;
        out.value = item.value.value_or(0.0f);
    }
    shared->count = n;
    InterlockedIncrement(&shared->sequence);
}

bool ReadSharedSensors(SensorList& out, int& count) {
    count = 0;
    unique_handle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, names::kSensorShareMapping));
    if (!mapping) {
        return false;
    }
    unique_handle_t<MappedViewTraits> view(MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, sizeof(SharedSensors)));
    if (!view) {
        return false;
    }
    const auto* shared = static_cast<const SharedSensors*>(view.get());
    // 主进程每秒最多写一次，几次重试足够碰上一个空档
    for (int attempt = 0; attempt < 8; ++attempt) {
        const LONG before = shared->sequence;
        if (before & 1) {
            Sleep(1);
            continue;
        }
        MemoryBarrier();
        const int n = std::clamp(static_cast<int>(shared->count), 0, kMaxSensorItems);
        for (int i = 0; i < n; ++i) {
            const SharedSensor& in = shared->items[i];
            SensorItem& item = out[static_cast<size_t>(i)];
            wcsncpy_s(item.key, in.key, _TRUNCATE);
            wcsncpy_s(item.name, in.name, _TRUNCATE);
            item.fan = in.fan != 0;
            item.value = in.has_value ? std::optional<float>(in.value) : std::nullopt;
        }
        MemoryBarrier();
        if (shared->sequence == before) {
            count = n;
            return true;
        }
    }
    return false;
}

}  // namespace pitot::ui
