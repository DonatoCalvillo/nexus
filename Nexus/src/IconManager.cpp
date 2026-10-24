#include "IconManager.h"

#include "AppIndexer.h"
#include "Constants.h"
#include "Overlay.h"

#include <algorithm>
#include <filesystem>
#include <shobjidl.h>

namespace fs = std::filesystem;

namespace nexus {

std::vector<VisibleIcon> g_visibleIcons;
int g_iconRetry = 0;
bool g_syncingIcons = false;

HICON IconFromBitmap(HBITMAP bitmap) {
    if (!bitmap) {
        return nullptr;
    }

    BITMAP bitmapInfo = {};
    if (!GetObject(bitmap, sizeof(bitmapInfo), &bitmapInfo)
        || bitmapInfo.bmWidth <= 0
        || bitmapInfo.bmHeight <= 0) {
        DeleteObject(bitmap);
        return nullptr;
    }

    HBITMAP mask = CreateBitmap(bitmapInfo.bmWidth, bitmapInfo.bmHeight, 1, 1, nullptr);
    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = bitmap;
    iconInfo.hbmMask = mask;
    HICON icon = CreateIconIndirect(&iconInfo);
    if (mask) {
        DeleteObject(mask);
    }
    DeleteObject(bitmap);
    return icon;
}

HICON IconFromPidl(PCIDLIST_ABSOLUTE pidl) {
    if (!pidl) {
        return nullptr;
    }

    SHFILEINFOW fileInfo = {};
    if (SHGetFileInfoW(
            reinterpret_cast<LPCWSTR>(pidl),
            0,
            &fileInfo,
            sizeof(fileInfo),
            SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON)
        && fileInfo.hIcon) {
        return fileInfo.hIcon;
    }

    // Some shell apps ignore SHGetFileInfo; fall back to IShellItemImageFactory.
    IShellItemImageFactory* factory = nullptr;
    if (FAILED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&factory))) || !factory) {
        return nullptr;
    }

    HBITMAP bitmap = nullptr;
    HRESULT result = factory->GetImage(SIZE{32, 32}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap);
    factory->Release();
    if (FAILED(result) || !bitmap) {
        return nullptr;
    }
    return IconFromBitmap(bitmap);
}

HICON GetAppIcon(const AppItem& app) {
    if (HICON icon = IconFromPidl(app.pidl)) {
        return icon;
    }
    if (app.path.empty()) {
        return nullptr;
    }

    std::error_code errorCode;
    if (fs::exists(app.path, errorCode) && !errorCode) {
        SHFILEINFOW fileInfo = {};
        if (SHGetFileInfoW(
                app.path.c_str(),
                0,
                &fileInfo,
                sizeof(fileInfo),
                SHGFI_ICON | SHGFI_SMALLICON)
            && fileInfo.hIcon) {
            return fileInfo.hIcon;
        }
    }

    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(SHParseDisplayName(app.path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
        HICON icon = IconFromPidl(pidl);
        CoTaskMemFree(pidl);
        return icon;
    }
    return nullptr;
}

void ReleaseVisibleIcons() {
    for (auto& entry : g_visibleIcons) {
        if (entry.icon) {
            DestroyIcon(entry.icon);
        }
    }
    g_visibleIcons.clear();
}

void SyncVisibleIcons() {
    if (g_syncingIcons) {
        return;
    }
    g_syncingIcons = true;

    std::vector<VisibleIcon> nextIcons;
    nextIcons.reserve(g_filteredApps.size());

    for (AppItem* app : g_filteredApps) {
        auto existing = std::find_if(
            g_visibleIcons.begin(),
            g_visibleIcons.end(),
            [app](const VisibleIcon& entry) { return entry.app == app && entry.icon; });

        if (existing != g_visibleIcons.end()) {
            nextIcons.push_back(*existing);
            existing->icon = nullptr;
        } else {
            nextIcons.push_back({app, GetAppIcon(*app)});
        }
    }

    ReleaseVisibleIcons();
    g_visibleIcons = std::move(nextIcons);

    bool hasMissingIcon = false;
    for (const auto& entry : g_visibleIcons) {
        if (!entry.icon) {
            hasMissingIcon = true;
            break;
        }
    }

    if (g_hwnd && hasMissingIcon && g_iconRetry < kIconRetryMax) {
        SetTimer(g_hwnd, kIconTimerId, 200, nullptr);
    } else if (g_hwnd) {
        KillTimer(g_hwnd, kIconTimerId);
    }

    g_syncingIcons = false;
}

}  // namespace nexus
