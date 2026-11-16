#pragma once

#include <windows.h>

namespace pitot::app {

bool LaunchPawnIoInstall(bool only_if_missing);
int RunPawnIoInstall(bool only_if_missing);

}  // namespace pitot::app
