#pragma once

#include "Types.h"

#include <string>
#include <vector>

namespace nexus {

	extern std::vector<AppItem> g_allApps;
	extern std::vector<AppItem*> g_filteredApps;

	std::wstring GetKnownFolderPath(REFKNOWNFOLDERID folderId);
	void ScanDirectory(const std::wstring& folderPath);
	void IndexAppsFolder();
	void ScanWindowsAppsAliases();
	void ReleaseIndexedApps();
	void IndexApplications();
	void UpdateFilter(const std::wstring& query);

}  // namespace nexus
