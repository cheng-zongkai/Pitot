#pragma once

#include <windows.h>

#include "common/unique_handle.h"
#include "ui/sensor_list.h"

// 传感器列表的跨进程共享（D-070）。设置窗口是另一个进程，自己读不到 PawnIO，
// 主进程每轮把 ListSensors 的结果写进命名共享内存，设置窗口按需读出来。
// 共享内存大小固定，写入是一次 memcpy，不分配堆内存。

namespace pitot::ui {

struct SharedSensor {
    sampler::SensorKey key;
    wchar_t name[48];
    float value;
    uint8_t fan;
    uint8_t has_value;
};

struct SharedSensors {
    // 写的时候先变成奇数，写完变成偶数；读的一方前后两次读到同一个偶数才算拿到完整的一份
    volatile LONG sequence;
    LONG count;
    SharedSensor items[kMaxSensorItems];
};

struct MappedViewTraits {
    using pointer = void*;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer p) noexcept { UnmapViewOfFile(p); }
};

// 主进程：创建共享内存，每轮发布一次
class SensorShareWriter {
public:
    bool Open();
    void Publish(const SensorList& list, int count);

private:
    unique_handle mapping_;
    unique_handle_t<MappedViewTraits> view_;
};

// 设置窗口：主进程没在运行或共享内存打不开时返回 false
bool ReadSharedSensors(SensorList& out, int& count);

}  // namespace pitot::ui
