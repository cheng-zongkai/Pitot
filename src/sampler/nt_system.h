#pragma once

#include <windows.h>

#include <winternl.h>

#include <cstddef>

// NtQuerySystemInformation 用到的、winternl.h 里没有完整公开的结构。
// 布局取自 phnt（System Informer 的 ntexapi.h），只用于 x64；字段偏移用 static_assert 钉住。
// phnt: Copyright (c) 2022 Winsider Seminars & Solutions, Inc., MIT License（全文见 THIRD_PARTY_NOTICES.md）。

namespace pitot::sampler::nt {

inline constexpr NTSTATUS kStatusSuccess = 0;
inline constexpr NTSTATUS kStatusInfoLengthMismatch = static_cast<NTSTATUS>(0xC0000004L);
inline constexpr NTSTATUS kStatusPrivilegeNotHeld = static_cast<NTSTATUS>(0xC0000061L);

inline constexpr auto kSystemProcessInformation = static_cast<SYSTEM_INFORMATION_CLASS>(5);
inline constexpr auto kSystemMemoryListInformation = static_cast<SYSTEM_INFORMATION_CLASS>(80);

// SYSTEM_PROCESS_INFORMATION 的前半部分，后面紧跟着线程数组（不用）
struct SystemProcessInfo {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    LONG BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR UniqueProcessKey;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
};
static_assert(offsetof(SystemProcessInfo, WorkingSetPrivateSize) == 8);
static_assert(offsetof(SystemProcessInfo, CreateTime) == 32);
static_assert(offsetof(SystemProcessInfo, ImageName) == 56);
static_assert(offsetof(SystemProcessInfo, UniqueProcessId) == 80);
static_assert(offsetof(SystemProcessInfo, WorkingSetSize) == 144);

// SYSTEM_MEMORY_LIST_INFORMATION，单位是页
struct SystemMemoryListInfo {
    ULONG_PTR ZeroPageCount;
    ULONG_PTR FreePageCount;
    ULONG_PTR ModifiedPageCount;
    ULONG_PTR ModifiedNoWritePageCount;
    ULONG_PTR BadPageCount;
    ULONG_PTR PageCountByPriority[8];  // 备用列表，按优先级
    ULONG_PTR RepurposedPagesByPriority[8];
    ULONG_PTR ModifiedPageCountPageFile;
};
static_assert(sizeof(SystemMemoryListInfo) == 22 * sizeof(ULONG_PTR));

}  // namespace pitot::sampler::nt
