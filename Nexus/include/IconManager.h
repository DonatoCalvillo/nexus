#pragma once

#include "Types.h"

#include <vector>

namespace nexus {

	extern std::vector<VisibleIcon> g_visibleIcons;
	extern int g_iconRetry;
	extern bool g_syncingIcons;

	HICON IconFromBitmap(HBITMAP bitmap);
	HICON IconFromPidl(PCIDLIST_ABSOLUTE pidl);
	HICON GetAppIcon(const AppItem& app);
	void ReleaseVisibleIcons();
	void SyncVisibleIcons();

}  // namespace nexus
