cmake_minimum_required(VERSION 3.28)
project(Pitot VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "Pitot 只支持 MSVC 编译")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# D-015：静态 CRT，单个 exe 即可运行
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 所有目标共用的编译选项，见 AGENTS.md“编码约定”
add_library(pitot_options INTERFACE)
target_compile_options(pitot_options INTERFACE /W4 /WX /permissive- /utf-8)
target_compile_definitions(pitot_options INTERFACE
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_include_directories(pitot_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 不碰窗口和硬件的纯逻辑，供主程序和测试共用
add_library(pitot_core STATIC
  src/config/config.cpp
  src/etw/events.cpp
  src/sampler/cpu_info.cpp
  src/sampler/process_stats.cpp
  src/sampler/smbios.cpp
  src/sampler/traffic.cpp
  src/sensors/board_chips.cpp
  src/sensors/decode.cpp
  src/sensors/parse.cpp
  src/ui/format.cpp
  src/ui/sensor_list.cpp
  src/ui/strings.cpp)
target_link_libraries(pitot_core PUBLIC pitot_options)

add_executable(Pitot WIN32
  src/app/autostart.cpp
  src/app/crash.cpp
  src/app/hot_notice.cpp
  src/app/main.cpp
  src/app/pawnio_install.cpp
  src/app/Pitot.manifest
  src/app/Pitot.rc
  src/common/log.cpp
  src/config/paths.cpp
  src/etw/kernel_trace.cpp
  src/sampler/demo.cpp
  src/sampler/detail.cpp
  src/sampler/gpu_processes.cpp
  src/sampler/icon_store.cpp
  src/sampler/net.cpp
  src/sampler/perf.cpp
  src/sampler/ping.cpp
  src/sampler/sampler.cpp
  src/sensors/board.cpp
  src/sensors/cpu.cpp
  src/sensors/gpu.cpp
  src/sensors/intel.cpp
  src/sensors/pawnio.cpp
  src/sensors/storage.cpp
  src/sensors/zen.cpp
  src/ui/flyout_window.cpp
  src/ui/logo.cpp
  src/ui/sensor_share.cpp
  src/ui/settings_window.cpp
  src/ui/strip_metrics.cpp
  src/ui/strip_window.cpp
  src/ui/system_theme.cpp
  src/ui/taskbar.cpp
  src/ui/theme.cpp)
target_link_libraries(Pitot PRIVATE pitot_core comctl32 comdlg32 d2d1 dwmapi dwrite imm32 iphlpapi ntdll pdh secur32 taskschd urlmon version winmm wintrust ws2_32)
# Ninja 不会自动跟踪 .rc 引用的头文件和二进制文件，手动声明，改了才会重新编资源
set_source_files_properties(src/app/Pitot.rc PROPERTIES OBJECT_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/app/resource.h;${CMAKE_CURRENT_SOURCE_DIR}/src/app/pitot.ico;${CMAKE_CURRENT_SOURCE_DIR}/third_party/pawnio/AMDFamily17.bin;${CMAKE_CURRENT_SOURCE_DIR}/third_party/pawnio/LpcIO.bin;${CMAKE_CURRENT_SOURCE_DIR}/third_party/pawnio/IntelMSR.bin")
# 清单里已经声明 requireAdministrator，关掉链接器自动生成的 UAC 段，避免两段冲突
target_link_options(Pitot PRIVATE /MANIFESTUAC:NO)
target_compile_definitions(Pitot PRIVATE "PITOT_VERSION=\"${PROJECT_VERSION}\"")

# D-033：可选的 PitotAdlx.dll，唯一用到 AMD ADLX SDK 的组件。
# ADLX 是 AMD 专有许可，头文件不入库；本地用 scripts/get-adlx.ps1 下载后才编这个 DLL。
# Pitot.exe 不依赖它：运行时找不到这个 DLL 就只显示显卡基础指标。
set(PITOT_ADLX_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/adlx/include")
if(EXISTS "${PITOT_ADLX_INCLUDE}/ADLX.h")
  add_library(PitotAdlx SHARED src/adlx/pitot_adlx.cpp)
  target_link_libraries(PitotAdlx PRIVATE pitot_options)
  target_include_directories(PitotAdlx PRIVATE "${PITOT_ADLX_INCLUDE}")
  message(STATUS "ADLX SDK found: building PitotAdlx.dll")
else()
  message(STATUS "ADLX SDK not found (scripts/get-adlx.ps1): PitotAdlx.dll skipped")
endif()

# D-081：PitotNvidia.dll。不含 NVIDIA 的头文件，运行时才加载驱动里的 nvml.dll。
# 没有 NVIDIA 显卡时 Pitot.exe 不会加载它。
add_library(PitotNvidia SHARED src/nvidia/pitot_nvidia.cpp)
target_link_libraries(PitotNvidia PRIVATE pitot_options)

# D-016：自写极简测试框架
enable_testing()
add_executable(pitot_tests
  tests/main.cpp
  tests/format_test.cpp
  tests/hot_watch_test.cpp
  tests/rate_test.cpp
  tests/board_chips_test.cpp
  tests/config_test.cpp
  tests/etw_test.cpp
  tests/flyout_logic_test.cpp
  tests/process_stats_test.cpp
  tests/parse_test.cpp
  tests/sensor_list_test.cpp
  tests/sensors_decode_test.cpp
  tests/nvidia_decode_test.cpp
  tests/settings_controls_test.cpp
  tests/smbios_test.cpp
  tests/storage_test.cpp
  tests/strings_test.cpp
  tests/traffic_test.cpp
  tests/triple_buffer_test.cpp
  tests/unique_handle_test.cpp)
target_link_libraries(pitot_tests PRIVATE pitot_core)
add_test(NAME pitot_tests COMMAND pitot_tests)
