#pragma once

#include <windows.h>
#include <shlobj.h>
#include <string>

namespace nexus {

    struct AppItem {
        std::wstring name;
        std::wstring lowerName;
        std::wstring path;
        // Shell identity for apps without a file on disk (e.g. Arc, Explorer). Not an HICON.
        PIDLIST_ABSOLUTE pidl = nullptr;
    };

    struct VisibleIcon {
        const AppItem* app = nullptr;
        HICON icon = nullptr;
    };

}  // namespace nexus
