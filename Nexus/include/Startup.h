#pragma once

#include <string>

namespace nexus {

bool IsStartupEnabled();
bool EnableStartup();
bool DisableStartup();
std::wstring GetExecutablePath();

}  // namespace nexus
