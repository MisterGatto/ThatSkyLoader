#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <format>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include "include/menu.hpp"
#include "include/mod_loader.h"
#include "include/json.hpp"

using json = nlohmann::json;

namespace ig = ImGui;

// Font configuration structure
struct FontConfig {
    std::string fontPath;
    float fontSize = 0.0f;
    unsigned int unicodeRangeStart = 0;
    unsigned int unicodeRangeEnd = 0;
} fontconfig;  // Global instance

namespace Menu {

/**
 * @brief Load font configuration from a JSON file
 * @param filename Path to the JSON configuration file
 * @param fontconfig Font configuration to populate
 */
void loadFontConfig(const std::string& filename, FontConfig& fontconfig) {
    fontconfig.fontPath = "C:\\Windows\\Fonts";
    fontconfig.fontSize = 18.0f;
    fontconfig.unicodeRangeStart = 0x0020;
    fontconfig.unicodeRangeEnd = 0x00FF;

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << filename << std::endl;
        return;
    }

    try {
        json jsonData;
        file >> jsonData;

        if (jsonData.contains("fontPath") && jsonData["fontPath"].is_string()) {
            fontconfig.fontPath = jsonData["fontPath"].get<std::string>();
        }

        if (jsonData.contains("fontSize") && jsonData["fontSize"].is_number()) {
            fontconfig.fontSize = jsonData["fontSize"].get<float>();
        }

        if (jsonData.contains("unicodeRangeStart")) {
            if (jsonData["unicodeRangeStart"].is_string()) {
                fontconfig.unicodeRangeStart = static_cast<ImWchar>(std::stoul(jsonData["unicodeRangeStart"].get<std::string>(), nullptr, 16));
            } else if (jsonData["unicodeRangeStart"].is_number_integer()) {
                fontconfig.unicodeRangeStart = static_cast<ImWchar>(jsonData["unicodeRangeStart"].get<unsigned int>());
            }
        }

        if (jsonData.contains("unicodeRangeEnd")) {
            if (jsonData["unicodeRangeEnd"].is_string()) {
                fontconfig.unicodeRangeEnd = static_cast<ImWchar>(std::stoul(jsonData["unicodeRangeEnd"].get<std::string>(), nullptr, 16));
            } else if (jsonData["unicodeRangeEnd"].is_number_integer()) {
                fontconfig.unicodeRangeEnd = static_cast<ImWchar>(jsonData["unicodeRangeEnd"].get<unsigned int>());
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error parsing JSON in loadFontConfig: " << e.what() << std::endl;
    }
    file.close();

    if (fontconfig.unicodeRangeStart == 0 || fontconfig.unicodeRangeEnd <= fontconfig.unicodeRangeStart) {
        fontconfig.unicodeRangeStart = 0x0020;
        fontconfig.unicodeRangeEnd = 0x00FF;
    }
}

/**
 * @brief Load fonts from the configured directory
 * @param fontconfig Font configuration to use
 */
void LoadFontsFromFolder(FontConfig& fontconfig) {
    namespace fs = std::filesystem;
    ImGuiIO& io = ImGui::GetIO();

    static ImWchar s_customRanges[3] = { 0x0020, 0x00FF, 0 };
    const ImWchar* ranges = nullptr;
    if (fontconfig.unicodeRangeStart > 0 && fontconfig.unicodeRangeEnd > fontconfig.unicodeRangeStart) {
        if (fontconfig.unicodeRangeStart == 0x0020 && fontconfig.unicodeRangeEnd == 0x00FF) {
            ranges = io.Fonts->GetGlyphRangesDefault();
        } else {
            s_customRanges[0] = static_cast<ImWchar>(fontconfig.unicodeRangeStart);
            s_customRanges[1] = static_cast<ImWchar>(fontconfig.unicodeRangeEnd);
            s_customRanges[2] = 0;
            ranges = s_customRanges;
        }
    } else {
        ranges = io.Fonts->GetGlyphRangesDefault();
    }

    if (fontconfig.fontSize <= 0.0f) {
        fontconfig.fontSize = 18.0f;
    }

    ImFontConfig fontCfg;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 2;
    fontCfg.PixelSnapH = true;

    if (!fontconfig.fontPath.empty() && fs::exists(fontconfig.fontPath) && fs::is_regular_file(fontconfig.fontPath)) {
        io.Fonts->AddFontFromFileTTF(fontconfig.fontPath.c_str(), fontconfig.fontSize, &fontCfg, ranges);
        return;
    }

    const std::string defaultFonts[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\calibri.ttf"
    };

    bool loaded = false;
    for (const auto& fontPath : defaultFonts) {
        if (fs::exists(fontPath)) {
            if (io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontconfig.fontSize, &fontCfg, ranges) != nullptr) {
                loaded = true;
            }
        }
    }

    if (!loaded) {
        io.Fonts->AddFontDefault();
    }
}

/**
 * @brief Display a font selector dropdown in the UI
 */
void ShowFontSelector() {
    ImGuiIO& io = ImGui::GetIO();
    ImFont* currentFont = ImGui::GetFont();
    
    if (ImGui::BeginCombo("Font", currentFont->GetDebugName())) {
        for (ImFont* font : io.Fonts->Fonts) {
            const bool isSelected = (font == currentFont);
            
            ImGui::PushID(static_cast<void*>(font));
            if (ImGui::Selectable(font->GetDebugName(), isSelected)) {
                io.FontDefault = font;  // Set as default font
            }
            
            // Set initial focus when opening the combo
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
            
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

/**
 * @brief Initialize ImGui context and setup
 * @param hwnd Window handle to attach ImGui to
 */
void InitializeContext(HWND hwnd) {
    // Prevent double initialization
    if (ig::GetCurrentContext()) {
        return;
    }

    // Create ImGui context
    ImGui::CreateContext();
    ImGui_ImplWin32_Init(hwnd);

    // Apply styling
    ImGuiStyle* style = &ImGui::GetStyle();
    style->WindowPadding = ImVec2(9, 9);
    style->FramePadding = ImVec2(9, 4);
    style->ItemSpacing = ImVec2(6, 4);
    style->ItemInnerSpacing = ImVec2(4, 4);
    style->IndentSpacing = 20;
    style->ScrollbarSize = 8;
    style->ScrollbarRounding = 12;
    style->GrabMinSize = 15;
    style->WindowBorderSize = 1;
    style->ChildBorderSize = 1;
    style->PopupBorderSize = 1;
    style->FrameBorderSize = 0;
    style->TabBorderSize = 1;
    style->TabBarBorderSize = 1;
    style->WindowRounding = 6;
    style->ChildRounding = 6;
    style->FrameRounding = 3;
    style->PopupRounding = 6;
    style->GrabRounding = 4;
    style->TabRounding = 4;
    style->CellPadding = ImVec2(2, 2);
    style->WindowTitleAlign = ImVec2(0.02f, 0.50f);
    style->SeparatorTextBorderSize = 2;
    style->SeparatorTextPadding = ImVec2(8, 0);

    ImVec4* colors = style->Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.21f, 0.21f, 0.21f, 0.18f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.73f);
    colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.18f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.29f, 0.29f, 0.29f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.53f, 0.53f, 0.53f, 0.67f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.54f, 0.54f, 0.54f, 0.31f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.69f, 0.69f, 0.69f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.83f, 0.83f, 0.83f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.29f, 0.29f, 0.29f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.29f, 0.29f, 0.29f, 0.50f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.29f, 0.29f, 0.29f, 0.50f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.52f, 0.52f, 0.52f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.67f, 0.67f, 0.67f, 0.50f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.74f, 0.74f, 0.74f, 0.95f);
    colors[ImGuiCol_Tab] = ImVec4(0.19f, 0.19f, 0.19f, 0.86f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.27f, 0.27f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.46f, 0.46f, 0.46f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.27f, 0.27f, 0.27f, 0.80f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.37f, 0.37f, 0.37f, 1.00f);
#ifdef ImGuiCol_DockingPreview
    colors[ImGuiCol_DockingPreview] = ImVec4(0.46f, 0.46f, 0.46f, 0.70f);
#endif
#ifdef ImGuiCol_DockingEmptyBg
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
#endif
    colors[ImGuiCol_PlotLines] = ImVec4(0.77f, 0.77f, 0.77f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.91f, 0.91f, 0.91f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.62f, 0.62f, 0.62f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.72f, 0.72f, 0.72f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.66f, 0.66f, 0.66f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.63f, 0.63f, 0.63f, 1.00f);

    std::string configPath = g_path.empty() ? "tsml_config.json" : g_path + "\\tsml_config.json";
    loadFontConfig(configPath, fontconfig);
    LoadFontsFromFolder(fontconfig);

    // Configure ImGui IO
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;  // Disable .ini file saving
}

/**
 * @brief Display a help marker with tooltip
 * @param description Text to display in the tooltip
 */
void HelpMarker(const char* description) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void EnsureConfigFileExists() {
    std::string filename = g_path.empty() ? "tsml_config.json" : g_path + "\\tsml_config.json";
    json jsonData;
    bool needsSave = false;

    std::ifstream inFile(filename);
    if (inFile.is_open()) {
        try {
            inFile >> jsonData;
        } catch (...) {
            jsonData = json::object();
        }
        inFile.close();
    } else {
        needsSave = true;
    }

    if (!jsonData.contains("file_format_version")) {
        jsonData["file_format_version"] = "1.0.0";
        needsSave = true;
    }
    if (!jsonData.contains("layer")) {
        json layerObj;
        layerObj["name"] = "VkLayer_TSML";
        layerObj["type"] = "GLOBAL";
        layerObj["api_version"] = "1.3.221";
        layerObj["library_path"] = ".\\powrprof.dll";
        layerObj["implementation_version"] = "1";
        layerObj["description"] = "That Sky Mod Loader";
        json funcsObj;
        funcsObj["vkGetInstanceProcAddr"] = "ModLoader_GetInstanceProcAddr";
        funcsObj["vkGetDeviceProcAddr"] = "ModLoader_GetDeviceProcAddr";
        layerObj["functions"] = funcsObj;
        json disEnv;
        disEnv["DISABLE_VKROOTS_TEST_1"] = "1";
        layerObj["disable_environment"] = disEnv;
        jsonData["layer"] = layerObj;
        needsSave = true;
    }
    if (!jsonData.contains("fontPath")) {
        jsonData["fontPath"] = "C:\\Windows\\Fonts";
        needsSave = true;
    }
    if (!jsonData.contains("fontSize")) {
        jsonData["fontSize"] = 18.0f;
        needsSave = true;
    }
    if (!jsonData.contains("unicodeRangeStart")) {
        jsonData["unicodeRangeStart"] = "0020";
        needsSave = true;
    }
    if (!jsonData.contains("unicodeRangeEnd")) {
        jsonData["unicodeRangeEnd"] = "00FF";
        needsSave = true;
    }
    if (!jsonData.contains("toggleKey")) {
        jsonData["toggleKey"] = VK_F5;
        needsSave = true;
    }
    if (!jsonData.contains("modStates")) {
        jsonData["modStates"] = json::object();
        needsSave = true;
    }

    if (needsSave) {
        std::ofstream outFile(filename);
        if (outFile.is_open()) {
            outFile << jsonData.dump(4);
            outFile.close();
        }
    }
}

void LoadConfig() {
    std::string filename = g_path.empty() ? "tsml_config.json" : g_path + "\\tsml_config.json";
    loadFontConfig(filename, fontconfig);
    std::ifstream file(filename);
    if (file.is_open()) {
        try {
            json jsonData;
            file >> jsonData;
            if (jsonData.contains("toggleKey")) {
                if (jsonData["toggleKey"].is_number_integer()) {
                    g_toggleKey = jsonData["toggleKey"].get<UINT>();
                } else if (jsonData["toggleKey"].is_string()) {
                    std::string keyStr = jsonData["toggleKey"].get<std::string>();
                    g_toggleKey = static_cast<UINT>(std::stoi(keyStr, nullptr, 0));
                }
            }
        } catch (...) {}
        file.close();
    }
}

void SaveConfig() {
    std::string filename = g_path.empty() ? "tsml_config.json" : g_path + "\\tsml_config.json";
    json jsonData;
    std::ifstream inFile(filename);
    if (inFile.is_open()) {
        try {
            inFile >> jsonData;
        } catch (...) {}
        inFile.close();
    }

    if (!jsonData.contains("file_format_version")) {
        jsonData["file_format_version"] = "1.0.0";
    }
    if (!jsonData.contains("layer")) {
        json layerObj;
        layerObj["name"] = "VkLayer_TSML";
        layerObj["type"] = "GLOBAL";
        layerObj["api_version"] = "1.3.221";
        layerObj["library_path"] = ".\\powrprof.dll";
        layerObj["implementation_version"] = "1";
        layerObj["description"] = "That Sky Mod Loader";
        json funcsObj;
        funcsObj["vkGetInstanceProcAddr"] = "ModLoader_GetInstanceProcAddr";
        funcsObj["vkGetDeviceProcAddr"] = "ModLoader_GetDeviceProcAddr";
        layerObj["functions"] = funcsObj;
        jsonData["layer"] = layerObj;
    }

    jsonData["fontPath"] = fontconfig.fontPath;
    jsonData["fontSize"] = fontconfig.fontSize;
    char hexStart[16], hexEnd[16], hexKey[16];
    snprintf(hexStart, sizeof(hexStart), "0x%X", fontconfig.unicodeRangeStart);
    snprintf(hexEnd, sizeof(hexEnd), "0x%X", fontconfig.unicodeRangeEnd);
    snprintf(hexKey, sizeof(hexKey), "0x%X", g_toggleKey);
    jsonData["unicodeRangeStart"] = std::string(hexStart);
    jsonData["unicodeRangeEnd"] = std::string(hexEnd);
    jsonData["toggleKey"] = std::string(hexKey);

    std::ofstream outFile(filename);
    if (outFile.is_open()) {
        outFile << jsonData.dump(4);
        outFile.close();
    }
}

struct HotkeyOption {
    const char* name;
    UINT vk;
};

static const HotkeyOption g_hotkeyOptions[] = {
    { "F5 (Default)", VK_F5 },
    { "F1", VK_F1 },
    { "F2", VK_F2 },
    { "F3", VK_F3 },
    { "F4", VK_F4 },
    { "F6", VK_F6 },
    { "F7", VK_F7 },
    { "F8", VK_F8 },
    { "F9", VK_F9 },
    { "F10", VK_F10 },
    { "F11", VK_F11 },
    { "F12", VK_F12 },
    { "Insert", VK_INSERT },
    { "Delete", VK_DELETE },
    { "Home", VK_HOME },
    { "End", VK_END },
    { "Tilde (~)", VK_OEM_3 },
    { "Section (§)", 0xDF }
};

void ShowHotkeySelector() {
    const char* currentLabel = "Unknown";
    for (const auto& opt : g_hotkeyOptions) {
        if (opt.vk == g_toggleKey) {
            currentLabel = opt.name;
            break;
        }
    }

    if (ImGui::BeginCombo("Menu Toggle Key", currentLabel)) {
        for (const auto& opt : g_hotkeyOptions) {
            const bool isSelected = (opt.vk == g_toggleKey);
            if (ImGui::Selectable(opt.name, isSelected)) {
                g_toggleKey = opt.vk;
                SaveConfig();
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

/**
 * @brief Display the main SML menu with mod list and settings
 */
void SMLMainMenu() {
    char buf[128];
    static char searchFilter[64] = "";
    ImGuiIO& io = ImGui::GetIO();

    ig::SetNextWindowSize({ 320, 0 }, ImGuiCond_Once);
    if (ig::Begin("That Sky Mod Loader", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SeparatorText(("Mods (" + std::to_string(ModLoader::GetModCount()) + ")").c_str());
        
        ig::InputText("Filter", searchFilter, sizeof(searchFilter));

        if (ig::Button("Reload All")) {
            ModLoader::ReloadAllMods();
        }
        ig::SameLine();
        if (ig::Button("Rescan Folder")) {
            ModLoader::LoadMods();
            ModLoader::LoadModStates();
        }

        ig::Spacing();

        ig::BeginTable("##mods", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody);
        ig::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch);
        ig::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Info").x);

        std::string filterStr = searchFilter;
        std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

        for (int i = 0; i < static_cast<int>(ModLoader::GetModCount()); i++) {
            std::string modName = std::string(ModLoader::GetModName(i));
            std::string modNameLower = modName;
            std::transform(modNameLower.begin(), modNameLower.end(), modNameLower.begin(), ::tolower);

            if (!filterStr.empty() && modNameLower.find(filterStr) == std::string::npos) {
                continue;
            }

            snprintf(buf, sizeof(buf), "%s##check%d", modName.c_str(), i);
            ig::TableNextColumn();
            if (ig::Checkbox(buf, &ModLoader::GetModEnabled(i))) {
                if (ModLoader::GetModEnabled(i)) {
                    ModLoader::EnableMod(i);
                }
                else {
                    ModLoader::DisableMod(i);
                }
            }
            ig::TableNextColumn();
            HelpMarker(ModLoader::toString(i).c_str());
        }
        ig::EndTable();
        ig::SeparatorText("Settings");

        ShowHotkeySelector();
        ShowFontSelector();
        ig::SameLine();
        HelpMarker(std::format("Total Fonts: {}\nPath: {}\nStart Range: {}\nEnd Range: {}\nConfig: tsml_config.json", 
            io.Fonts->Fonts.Size, fontconfig.fontPath.c_str(), fontconfig.unicodeRangeStart, 
            fontconfig.unicodeRangeEnd).c_str());

        const float MIN_SCALE = 0.3f;
        const float MAX_SCALE = 3.0f;
        static float window_scale = 1.0f;
        if (ig::DragFloat("Window Scale", &window_scale, 0.005f, MIN_SCALE, MAX_SCALE, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            ig::SetWindowFontScale(window_scale);
        ig::DragFloat("Global Scale", &io.FontGlobalScale, 0.005f, MIN_SCALE, MAX_SCALE, "%.2f", ImGuiSliderFlags_AlwaysClamp);

        ig::Spacing();
        ig::Separator();
        ig::Spacing();

        ig::Text("v0.2.5 | FPS: %.f | %.2f ms", io.Framerate, 1000.0f / io.Framerate);
    }
    ig::End();
}

/**
 * @brief Render the menu if enabled
 */
void Render() {
    if (!bShowMenu)
        return;

    SMLMainMenu();
    ModLoader::RenderAll();
}
}