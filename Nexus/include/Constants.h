#pragma once

#include <windows.h>

namespace nexus {

constexpr int kHotkeyId = 1;
constexpr UINT_PTR kIconTimerId = 2;
constexpr int kIconRetryMax = 5;
constexpr int kWindowWidth = 640;
constexpr int kHeaderHeight = 70;
constexpr int kItemHeight = 44;
constexpr size_t kMaxResults = 5;

constexpr int kEditControlId = 101;
constexpr UINT kLaunchSelectedMessage = WM_APP + 1;
constexpr UINT kShowOverlayMessage = WM_APP + 2;
constexpr wchar_t kWindowClassName[] = L"NexusOverlayClass";
constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\NexusLauncherSingleInstance";

}  // namespace nexus
