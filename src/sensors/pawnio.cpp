#include "sensors/pawnio.h"

#include <winioctl.h>

#include <cstring>

#include "common/names.h"

namespace pitot::sensors {
namespace {

// pawnio_um.h：设备类型 41394，METHOD_BUFFERED
constexpr DWORD kDeviceType = 41394;
constexpr DWORD kIoctlLoadBinary = CTL_CODE(kDeviceType, 0x821, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr DWORD kIoctlExecute = CTL_CODE(kDeviceType, 0x841, METHOD_BUFFERED, FILE_ANY_ACCESS);
// 输入缓冲：前 32 字节是 0 结尾的函数名，后面紧跟参数（PawnIOLib.cpp FN_NAME_LENGTH）
constexpr size_t kNameLength = 32;
constexpr size_t kMaxArgs = 4;

}  // namespace

DWORD PawnIoModule::Open(int resource_id) {
    Close();
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(resource_id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    HGLOBAL loaded = res ? LoadResource(nullptr, res) : nullptr;
    const void* blob = loaded ? LockResource(loaded) : nullptr;
    const DWORD size = res ? SizeofResource(nullptr, res) : 0;
    if (!blob || size == 0) {
        return ERROR_RESOURCE_DATA_NOT_FOUND;
    }

    device_.reset(CreateFileW(names::kPawnIoDevice, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                              nullptr));
    if (!device_) {
        return GetLastError();
    }
    DWORD returned = 0;
    // 资源是只读映射，驱动只读输入缓冲，强转去掉 const 是安全的
    if (!DeviceIoControl(device_.get(), kIoctlLoadBinary, const_cast<void*>(blob), size, nullptr, 0, &returned,
                         nullptr)) {
        const DWORD error = GetLastError();
        device_.reset();
        return error;
    }
    return ERROR_SUCCESS;
}

bool PawnIoModule::Execute(const char* function, const uint64_t* in, size_t in_count, uint64_t* out,
                           size_t out_count) {
    if (!device_ || in_count > kMaxArgs || out_count > kMaxArgs || strlen(function) >= kNameLength) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    // 栈上缓冲，不分配堆
    alignas(8) unsigned char buffer[kNameLength + kMaxArgs * sizeof(uint64_t)]{};
    memcpy(buffer, function, strlen(function));
    if (in_count) {
        memcpy(buffer + kNameLength, in, in_count * sizeof(uint64_t));
    }
    DWORD returned = 0;
    return DeviceIoControl(device_.get(), kIoctlExecute, buffer,
                           static_cast<DWORD>(kNameLength + in_count * sizeof(uint64_t)), out,
                           static_cast<DWORD>(out_count * sizeof(uint64_t)), &returned, nullptr) != FALSE;
}

bool SharedMutex::Open(const wchar_t* name) {
    // FanControl 等先建了就打开已有的；没有就自己建，别人再来打开
    mutex_.reset(CreateMutexW(nullptr, FALSE, name));
    if (!mutex_) {
        mutex_.reset(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name));
    }
    return static_cast<bool>(mutex_);
}

bool SharedMutex::TryLock(DWORD timeout_ms) {
    if (!mutex_) {
        return false;
    }
    // 别的进程拿着锁崩溃了会返回 WAIT_ABANDONED，此时锁已归我们
    const DWORD result = WaitForSingleObject(mutex_.get(), timeout_ms);
    return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
}

void SharedMutex::Unlock() {
    if (mutex_) {
        ReleaseMutex(mutex_.get());
    }
}

}  // namespace pitot::sensors
