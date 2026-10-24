#pragma once

#include <windows.h>

namespace nexus {

    extern HWND g_hwnd;
    extern HWND g_hwndEdit;
    extern HFONT g_hFont;
    extern HFONT g_hFontList;
    extern HBRUSH g_bgBrush;
    extern int g_selectedIndex;

    void ApplyWindowEffects(HWND hwnd);
    bool CreateOverlayWindow(HINSTANCE instance);
    void ShowOverlayWindow();
    void RequestShowOverlay();
    void CloseOverlay();
    void LaunchSelected();
    int RunMessageLoop();
    HWND FindExistingOverlayWindow();
    LRESULT CALLBACK EditSubclassProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR refData);
    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

}  // namespace nexus
