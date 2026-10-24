#include "AppIndexer.h"

#include "Constants.h"
#include "IconManager.h"
#include "Overlay.h"
#include "StringUtils.h"

#include <filesystem>
#include <shobjidl.h>

namespace fs = std::filesystem;

namespace nexus {

    std::vector<AppItem> g_allApps;
    std::vector<AppItem*> g_filteredApps;

    std::wstring GetKnownFolderPath(REFKNOWNFOLDERID folderId) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &path))) {
            std::wstring result(path);
            CoTaskMemFree(path);
            return result;
        }
        return L"";
    }

    static bool HasAppWithName(const std::wstring& name, bool caseInsensitive) {
        for (const auto& app : g_allApps) {
            if (caseInsensitive) {
                if (_wcsicmp(app.name.c_str(), name.c_str()) == 0) {
                    return true;
                }
            } else if (app.name == name) {
                return true;
            }
        }
        return false;
    }

    void ScanDirectory(const std::wstring& folderPath) {
        if (folderPath.empty() || !fs::exists(folderPath)) {
            return;
        }

        std::error_code errorCode;
        for (const auto& entry : fs::recursive_directory_iterator(
                 folderPath,
                 fs::directory_options::skip_permission_denied,
                 errorCode)) {
            if (errorCode) {
                continue;
            }

            if (entry.is_regular_file() && entry.path().extension() == L".lnk") {
                std::wstring name = entry.path().stem().wstring();
                std::wstring fullPath = entry.path().wstring();

                if (HasAppWithName(name, false)) {
                    continue;
                }

                g_allApps.push_back({
                    name,
                    ToLower(name),
                    fullPath,
                });
            }
        }
    }

    void IndexAppsFolder() {
        IShellItem* appsFolder = nullptr;
        HRESULT result = SHCreateItemInKnownFolder(
            FOLDERID_AppsFolder,
            0,
            nullptr,
            IID_PPV_ARGS(&appsFolder));
        if (FAILED(result) || !appsFolder) {
            return;
        }

        IEnumShellItems* enumerator = nullptr;
        result = appsFolder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&enumerator));
        if (FAILED(result) || !enumerator) {
            appsFolder->Release();
            return;
        }

        IShellItem* item = nullptr;
        while (enumerator->Next(1, &item, nullptr) == S_OK) {
            PWSTR displayName = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &displayName)) && displayName) {
                std::wstring name(displayName);
                CoTaskMemFree(displayName);

                PWSTR parsingPath = nullptr;
                std::wstring launchPath;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsingPath))
                    && parsingPath) {
                    launchPath = parsingPath;
                    CoTaskMemFree(parsingPath);
                }

                if (!HasAppWithName(name, true) && !launchPath.empty()) {
                    PIDLIST_ABSOLUTE pidl = nullptr;
                    if (FAILED(SHGetIDListFromObject(item, &pidl))) {
                        pidl = nullptr;
                    }

                    g_allApps.push_back({
                        name,
                        ToLower(name),
                        launchPath,
                        pidl,
                    });
                }
            }
            item->Release();
        }

        enumerator->Release();
        appsFolder->Release();
    }

    void ScanWindowsAppsAliases() {
        std::wstring localAppData = GetKnownFolderPath(FOLDERID_LocalAppData);
        if (localAppData.empty()) {
            return;
        }

        fs::path windowsAppsPath = fs::path(localAppData) / L"Microsoft" / L"WindowsApps";

        std::error_code errorCode;
        if (!fs::exists(windowsAppsPath, errorCode) || errorCode) {
            return;
        }

        for (auto it = fs::directory_iterator(
                 windowsAppsPath,
                 fs::directory_options::skip_permission_denied,
                 errorCode);
             it != fs::directory_iterator();
             it.increment(errorCode)) {
            if (errorCode) {
                errorCode.clear();
                continue;
            }

            try {
                const auto& entry = *it;

                std::error_code fileError;
                if (entry.is_regular_file(fileError) && !fileError && entry.path().extension() == L".exe") {
                    std::wstring name = entry.path().stem().wstring();

                    if (HasAppWithName(name, true)) {
                        continue;
                    }

                    std::wstring fullPath = entry.path().wstring();
                    g_allApps.push_back({
                        name,
                        ToLower(name),
                        fullPath,
                    });
                }
            } catch (...) {
                continue;
            }
        }
    }

    void ReleaseIndexedApps() {
        ReleaseVisibleIcons();
        for (auto& app : g_allApps) {
            if (app.pidl) {
                CoTaskMemFree(app.pidl);
            }
        }
        g_allApps.clear();
        g_filteredApps.clear();
    }

    void IndexApplications() {
        ReleaseIndexedApps();

        IndexAppsFolder();

        std::wstring userPrograms = GetKnownFolderPath(FOLDERID_Programs);
        ScanDirectory(userPrograms);
        std::wstring commonPrograms = GetKnownFolderPath(FOLDERID_CommonPrograms);
        ScanDirectory(commonPrograms);
    }

    void UpdateFilter(const std::wstring& query) {
        g_filteredApps.clear();
        std::wstring lowerQuery = ToLower(query);

        for (auto& app : g_allApps) {
            if (g_filteredApps.size() >= kMaxResults) {
                break;
            }

            if (lowerQuery.empty() || app.lowerName.find(lowerQuery) != std::wstring::npos) {
                g_filteredApps.push_back(&app);
            }
        }

        g_selectedIndex = 0;
        g_iconRetry = 0;
        SyncVisibleIcons();

        int newHeight = kHeaderHeight + static_cast<int>(g_filteredApps.size()) * kItemHeight;
        SetWindowPos(
            g_hwnd,
            nullptr,
            0,
            0,
            kWindowWidth,
            newHeight,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(g_hwnd, nullptr, TRUE);
    }

}  // namespace nexus
