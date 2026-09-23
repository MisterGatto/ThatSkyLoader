#pragma once

#include <cstddef>
#include <windows.h>
#include <vector>
#include <string>
#include <string_view>
#include <chrono>

#include "api.h"

typedef void (*StartFn)();
typedef void (*OnEnableFn)();
typedef void (*OnDisableFn)();
typedef void (*StartFn)();
typedef void (*GetModInfoFn)(ModInfo& info);
typedef void (*RenderFn)();

enum Fn_Idx{
    START_FN = 0,
    GET_MOD_INFO_FN,
    RENDER_FN,
    ON_ENABLE_FN,
    ON_DISABLE_FN
};

struct ModItem {
    HMODULE hModule;
    std::string filePath;
    StartFn start;
    GetModInfoFn getInfo;
    RenderFn render;
    OnEnableFn onEnable;
    OnDisableFn onDisable;

    ModInfo info;
    bool enabled;
    bool initialized;
    ModItem(HMODULE hModule, const std::string& filePath = "")
        : hModule(hModule), filePath(filePath), start(nullptr), getInfo(nullptr),
          render(nullptr), onEnable(nullptr), onDisable(nullptr), enabled(true), initialized(false) {}
};

class ModLoader {
    static std::vector<ModItem> mods;

    // Helper methods
    static std::string GetModsDirectory();
    static bool EnsureModsDirectoryExists(const std::string& directory);
    static bool LoadModFromFile(const std::string& filePath);
    static void LogSystemInfo();

public:
    // Core functionality
    static void LoadMods();
    static void UnloadAllMods();
    static void ReloadAllMods();
    static bool ReloadMod(int index);
    static void RenderAll();
    static void SaveModStates();
    static void LoadModStates();
    
    // Mod information and management
    static size_t GetModCount();
    static const ModInfo& GetModInfo(int index);
    static std::string_view GetModName(int index);
    static bool& GetModEnabled(int index);
    static void Render(int index);
    static void EnableMod(int index);
    static void DisableMod(int index);
    static std::string toString(int index);
};