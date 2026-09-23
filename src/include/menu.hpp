#pragma once

#include <Windows.h>
#include <string>

namespace Menu {
    void InitializeContext(HWND hwnd);
    void Render();
    void EnsureConfigFileExists();
    void LoadConfig();
    void SaveConfig();

    inline bool bShowMenu = true;
    inline UINT g_toggleKey = VK_F5;
    inline std::string g_path;
} // namespace Menu

