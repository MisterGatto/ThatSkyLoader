// Windows headers must come before standard headers to avoid redefinition errors
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Standard C++ headers
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <system_error>
#include <iomanip>      // For std::setw and std::setfill

#include <setjmp.h>
#include <functional>

#include "include/mod_loader.h"
#include "include/json.hpp"
#include "include/menu.hpp"

static bool g_InModCallback = false;
static jmp_buf g_ModCallbackEnv;

static LONG WINAPI SafeModExceptionHandler(EXCEPTION_POINTERS* pInfo) {
    if (g_InModCallback && pInfo && pInfo->ExceptionRecord) {
        DWORD code = pInfo->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
            std::cerr << "[ERROR] Caught hardware exception 0x" << std::hex << code << std::dec << " inside mod callback!" << std::endl << std::flush;
            longjmp(g_ModCallbackEnv, 1);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool SafeCallMod(std::function<void()> func) {
    static bool handlerRegistered = false;
    if (!handlerRegistered) {
        AddVectoredExceptionHandler(1, SafeModExceptionHandler);
        handlerRegistered = true;
    }

    bool success = false;
    g_InModCallback = true;
    if (setjmp(g_ModCallbackEnv) == 0) {
        try {
            func();
            success = true;
        } catch (const std::exception& e) {
            std::cerr << "C++ Exception in mod callback: " << e.what() << std::endl << std::flush;
            success = false;
        } catch (...) {
            std::cerr << "Unknown C++ Exception in mod callback" << std::endl << std::flush;
            success = false;
        }
    } else {
        success = false;
    }
    g_InModCallback = false;
    return success;
}

// Static member initialization
std::vector<ModItem> ModLoader::mods;

/**
 * @brief Logs basic system information for debugging
 */
void ModLoader::LogSystemInfo() {
    try {
        std::cout << "\n--------- System Information ---------" << std::endl;
        
        // Get current process path and directory
        char processPath[MAX_PATH];
        if (GetModuleFileNameA(NULL, processPath, MAX_PATH) > 0) {
            std::cout << "Process path: " << processPath << std::endl;
            
            std::string fullPath(processPath);
            std::string processDir = fullPath.substr(0, fullPath.find_last_of("\\/"));
            std::cout << "Process directory: " << processDir << std::endl;
        }
        
        // CPU architecture
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        std::cout << "Processor architecture: ";
        switch (sysInfo.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_AMD64: std::cout << "x64"; break;
            case PROCESSOR_ARCHITECTURE_ARM: std::cout << "ARM"; break;
            case PROCESSOR_ARCHITECTURE_ARM64: std::cout << "ARM64"; break;
            case PROCESSOR_ARCHITECTURE_INTEL: std::cout << "x86"; break;
            default: std::cout << "Unknown " << sysInfo.wProcessorArchitecture;
        }
        std::cout << std::endl;
        
        // Available memory
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            std::cout << "Memory load: " << memInfo.dwMemoryLoad << "%" << std::endl;
            std::cout << "Total physical memory: " << (memInfo.ullTotalPhys / (1024*1024)) << " MB" << std::endl;
            std::cout << "Available physical memory: " << (memInfo.ullAvailPhys / (1024*1024)) << " MB" << std::endl;
        }
        
        // DLL load address
        HMODULE hModule = GetModuleHandleA(NULL);
        std::cout << "Application base address: 0x" << std::hex << (uintptr_t)hModule << std::dec << std::endl;
        
        std::cout << "\n--------- Directory Contents ---------" << std::endl;
        // Get and list files in the process directory
        if (GetModuleFileNameA(NULL, processPath, MAX_PATH) > 0) {
            std::string fullPath(processPath);
            std::string processDir = fullPath.substr(0, fullPath.find_last_of("\\/"));
            
            WIN32_FIND_DATAA findData;
            HANDLE hFind = FindFirstFileA((processDir + "\\*").c_str(), &findData);
            
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
                        std::string fullItemPath = processDir + "\\" + findData.cFileName;
                        std::cout << "- " << findData.cFileName;
                        
                        // Get file attributes
                        DWORD attrs = GetFileAttributesA(fullItemPath.c_str());
                        if (attrs != INVALID_FILE_ATTRIBUTES) {
                            if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                                std::cout << " [DIR]";
                            } else {
                                // Get file size for non-directories
                                LARGE_INTEGER fileSize;
                                fileSize.LowPart = findData.nFileSizeLow;
                                fileSize.HighPart = findData.nFileSizeHigh;
                                std::cout << " [" << fileSize.QuadPart << " bytes]";
                            }
                        }
                        std::cout << std::endl;
                    }
                } while (FindNextFileA(hFind, &findData));
                
                FindClose(hFind);
            }
        }
        
        // Check for mods directory specifically
        std::string modsDir = GetModsDirectory();
        std::cout << "\n--------- Mods Directory Contents ---------" << std::endl;
        std::cout << "Mods directory: " << modsDir << std::endl;
        
        // Check if mods directory exists
        DWORD modsDirAttrs = GetFileAttributesA(modsDir.c_str());
        if (modsDirAttrs != INVALID_FILE_ATTRIBUTES && (modsDirAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
            WIN32_FIND_DATAA findData;
            HANDLE hFind = FindFirstFileA((modsDir + "\\*").c_str(), &findData);
            
            if (hFind != INVALID_HANDLE_VALUE) {
                bool foundAnyMods = false;
                
                do {
                    if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
                        foundAnyMods = true;
                        std::string fullItemPath = modsDir + "\\" + findData.cFileName;
                        std::cout << "- " << findData.cFileName;
                        
                        // Get file attributes
                        DWORD attrs = GetFileAttributesA(fullItemPath.c_str());
                        if (attrs != INVALID_FILE_ATTRIBUTES) {
                            if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                                std::cout << " [DIR]";
                            } else {
                                // Check if it's a DLL
                                std::string extension = findData.cFileName;
                                size_t pos = extension.find_last_of(".");
                                if (pos != std::string::npos) {
                                    extension = extension.substr(pos);
                                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                                    
                                    if (extension == ".dll") {
                                        std::cout << " [DLL]";
                                    }
                                }
                                
                                // Get file size
                                LARGE_INTEGER fileSize;
                                fileSize.LowPart = findData.nFileSizeLow;
                                fileSize.HighPart = findData.nFileSizeHigh;
                                std::cout << " [" << fileSize.QuadPart << " bytes]";
                            }
                        }
                        std::cout << std::endl;
                    }
                } while (FindNextFileA(hFind, &findData));
                
                if (!foundAnyMods) {
                    std::cout << "No files found in mods directory" << std::endl;
                }
                
                FindClose(hFind);
            } else {
                std::cerr << "Failed to enumerate mods directory contents, error: " << GetLastError() << std::endl;
            }
        } else {
            std::cerr << "Mods directory does not exist or is not accessible" << std::endl;
        }
        
        std::cout << "\n--------- End System Information ---------\n" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error gathering system information: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error occurred while gathering system information" << std::endl;
    }
}

std::string ModLoader::GetModsDirectory() {
    try {
        const std::string modsFolder = "mods";
        char buffer[MAX_PATH];
        
        if (GetModuleFileNameA(NULL, buffer, MAX_PATH) == 0) {
            throw std::system_error(GetLastError(), std::system_category(), "Failed to get module filename");
        }
        
        std::string::size_type pos = std::string(buffer).find_last_of("\\/");
        if (pos == std::string::npos) {
            throw std::runtime_error("Failed to find directory separator in path");
        }
        
        return std::string(buffer).substr(0, pos) + "\\" + modsFolder;
    } catch (const std::exception& e) {
        std::cout << "Error getting mods directory: " << e.what() << std::endl;
        return "mods"; // Fallback to relative path
    }
}

bool ModLoader::EnsureModsDirectoryExists(const std::string& directory) {
    try {
        DWORD attributes = GetFileAttributesA(directory.c_str());
        
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            std::cout << "Creating mods directory: " << directory << std::endl;
            if (!CreateDirectoryA(directory.c_str(), NULL)) {
                throw std::system_error(GetLastError(), std::system_category(), "Failed to create mods directory");
            }
            return true;
        }
        
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            throw std::runtime_error("Mods path exists but is not a directory");
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cout << "Error ensuring mods directory exists: " << e.what() << std::endl;
        return false;
    }
}

bool ModLoader::LoadModFromFile(const std::string& filePath) {
    try {
        // Check if file exists before attempting to load
        DWORD fileAttributes = GetFileAttributesA(filePath.c_str());
        if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
            DWORD error = GetLastError();
            std::cerr << "Error accessing mod file: " << filePath << ", Error code: 0x" 
                      << std::hex << error << std::dec << " (" << error << ")" << std::endl;
            if (error == ERROR_FILE_NOT_FOUND) {
                std::cerr << "File not found: " << filePath << std::endl;
            } else if (error == ERROR_PATH_NOT_FOUND) {
                std::cerr << "Path not found: " << filePath << std::endl;
            } else if (error == ERROR_ACCESS_DENIED) {
                std::cerr << "Access denied to file: " << filePath << std::endl;
            }
            return false;
        }
        
        if (!(fileAttributes & FILE_ATTRIBUTE_NORMAL) && (fileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::cerr << "Path is a directory, not a file: " << filePath << std::endl;
            return false;
        }

        std::cout << "Loading mod: " << filePath << std::endl;
        
        // Get file size for verification
        WIN32_FILE_ATTRIBUTE_DATA fileData;
        if (GetFileAttributesExA(filePath.c_str(), GetFileExInfoStandard, &fileData)) {
            LARGE_INTEGER fileSize;
            fileSize.HighPart = fileData.nFileSizeHigh;
            fileSize.LowPart = fileData.nFileSizeLow;
            std::cout << "Mod file size: " << fileSize.QuadPart << " bytes" << std::endl;
        }
        HMODULE hModule = LoadLibraryExA(filePath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!hModule) {
            hModule = LoadLibraryA(filePath.c_str());
        }
        if (!hModule) {
            DWORD error = GetLastError();
            std::cerr << "Failed to load DLL: " << filePath << ", Error code: 0x" 
                      << std::hex << error << std::dec << " (" << error << ")" << std::endl;
            
            // Provide more specific error messages for common error codes
            if (error == ERROR_BAD_EXE_FORMAT) {
                std::cerr << "The file is not a valid DLL or executable" << std::endl;
            } else if (error == ERROR_MOD_NOT_FOUND) {
                std::cerr << "A required module was not found" << std::endl;
            } else if (error == ERROR_DLL_INIT_FAILED) {
                std::cerr << "DLL initialization failed" << std::endl;
            }
            return false;
        }
        
        mods.emplace_back(hModule, filePath);
        ModItem& item = mods.back();
        
        // Load function pointers and log results
        item.start = reinterpret_cast<StartFn>(GetProcAddress(hModule, "Start"));
        std::cout << "Start function found: " << (item.start ? "Yes" : "No") << std::endl;
        
        item.onDisable = reinterpret_cast<OnDisableFn>(GetProcAddress(hModule, "onDisable"));
        std::cout << "onDisable function found: " << (item.onDisable ? "Yes" : "No") << std::endl;
        
        item.onEnable = reinterpret_cast<OnEnableFn>(GetProcAddress(hModule, "onEnable"));
        std::cout << "onEnable function found: " << (item.onEnable ? "Yes" : "No") << std::endl;
        
        item.getInfo = reinterpret_cast<GetModInfoFn>(GetProcAddress(hModule, "GetModInfo"));
        std::cout << "GetModInfo function found: " << (item.getInfo ? "Yes" : "No") << std::endl;
        
        item.render = reinterpret_cast<RenderFn>(GetProcAddress(hModule, "Render"));
        std::cout << "Render function found: " << (item.render ? "Yes" : "No") << std::endl;
        
        std::string fileName = filePath;
        size_t pos = filePath.find_last_of("\\/");
        if (pos != std::string::npos) {
            fileName = filePath.substr(pos + 1);
        }

        // Initialize mod info and start if available
        if (item.getInfo) {
            try {
                item.getInfo(item.info);
                std::cout << "Loaded mod info for: " << fileName << std::endl;
            } catch (...) {
                std::cerr << "Warning: Exception calling GetModInfo for: " << fileName << std::endl;
            }
        } else {
            std::cerr << "Warning: Mod does not provide GetModInfo function: " << fileName << std::endl;
        }
        std::cout << "[+] Mod registered and ready: " << fileName << std::endl << std::flush;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading mod " << filePath << ": " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Unknown error occurred while loading mod: " << filePath << std::endl;
        return false;
    }
}

void ModLoader::LoadMods() {
    try {
        std::cout << "Starting mod loading process" << std::endl;
        
        // Log system information for debugging
        LogSystemInfo();
        
        // Additional basic system info that's important for troubleshooting
        std::cout << "\n--------- Loading Environment ---------" << std::endl;
        char currentDir[MAX_PATH];
        if (GetCurrentDirectoryA(MAX_PATH, currentDir)) {
            std::cout << "Current working directory: " << currentDir << std::endl;
        }
        
        // Get and ensure mods directory exists
        std::string modsDirectory = GetModsDirectory();
        std::cout << "Mods directory path: " << modsDirectory << std::endl;
        
        // Check if mods directory is accessible and has correct permissions
        DWORD attrs = GetFileAttributesA(modsDirectory.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            DWORD error = GetLastError();
            std::cerr << "Error accessing mods directory: 0x" << std::hex << error << std::dec 
                      << " (" << error << ")" << std::endl;
        } else {
            std::cout << "Mods directory attributes: " 
                      << (attrs & FILE_ATTRIBUTE_DIRECTORY ? "DIRECTORY " : "") 
                      << (attrs & FILE_ATTRIBUTE_READONLY ? "READONLY " : "") 
                      << (attrs & FILE_ATTRIBUTE_HIDDEN ? "HIDDEN " : "") 
                      << (attrs & FILE_ATTRIBUTE_SYSTEM ? "SYSTEM " : "") 
                      << std::endl;
        }
        
        if (!EnsureModsDirectoryExists(modsDirectory)) {
            std::cerr << "Failed to ensure mods directory exists, mod loading aborted" << std::endl;
            return;
        }
        
        // Clear existing mods if reloading
        if (!mods.empty()) {
            std::cout << "Unloading existing mods before loading new ones" << std::endl;
            UnloadAllMods();
        }
        
        // Load all DLL files from the mods directory
        size_t loadedCount = 0;
        size_t failedCount = 0;
        
        for (const auto& entry : std::filesystem::directory_iterator(modsDirectory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".dll") {
                std::string filePath = entry.path().string();
                if (LoadModFromFile(filePath)) {
                    loadedCount++;
                } else {
                    failedCount++;
                }
            }
        }
        
        for (size_t i = 0; i < mods.size(); ++i) {
            EnableMod(static_cast<int>(i));
        }
        
        std::cout << "Mod loading complete. Loaded: " << loadedCount << ", Failed: " << failedCount << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Critical error during mod loading: " << e.what() << std::endl;
    }
}

size_t ModLoader::GetModCount() {
    return mods.size();
}

const ModInfo& ModLoader::GetModInfo(int index) {
    if (index < 0 || index >= static_cast<int>(mods.size())) {
        static ModInfo emptyInfo;
        std::cout << "Error: Attempted to access mod info with invalid index: " << index << std::endl;
        return emptyInfo;
    }
    return mods[index].info;
}

void ModLoader::Render(int index) {
    if (index < 0 || index >= static_cast<int>(mods.size())) {
        return;
    }
    
    if (mods[index].enabled && mods[index].render) {
        SafeCallMod([&]() { mods[index].render(); });
    }
}

void ModLoader::EnableMod(int index) {
    if (index < 0 || index >= static_cast<int>(mods.size())) {
        return;
    }
    
    if (!mods[index].initialized) {
        mods[index].initialized = true;
        if (mods[index].start) {
            std::cout << "[+] Calling Start() on EnableMod for: " << mods[index].filePath << std::endl << std::flush;
            SafeCallMod([&]() { mods[index].start(); });
        }
    }

    if (mods[index].onEnable) {
        SafeCallMod([&]() { mods[index].onEnable(); });
    }
    mods[index].enabled = true;
    SaveModStates();
}

void ModLoader::DisableMod(int index) {
    if (index < 0 || index >= static_cast<int>(mods.size())) {
        return;
    }
    
    if (mods[index].onDisable) {
        SafeCallMod([&]() { mods[index].onDisable(); });
    }
    mods[index].enabled = false;
    SaveModStates();
}

bool& ModLoader::GetModEnabled(int index) {
    if (index < 0 || index >= static_cast<int>(mods.size())) {
        static bool dummyEnabled = false;
        std::cout << "Error: Attempted to access mod enabled status with invalid index: " << index << std::endl;
        return dummyEnabled;
    }
    return mods[index].enabled;
}

std::string_view ModLoader::GetModName(int index) {
    if (index < 0 || index >= static_cast<int>(mods.size())) {
        static std::string emptyName = "<invalid mod>";
        return emptyName;
    }
    if (!mods[index].info.name.empty()) {
        return mods[index].info.name;
    }
    static std::string fallbackName;
    std::filesystem::path p(mods[index].filePath);
    fallbackName = p.filename().string();
    return fallbackName;
}

void ModLoader::RenderAll() {
    try {
        for (size_t i = 0; i < mods.size(); i++) {
            if (!mods[i].initialized && mods[i].enabled) {
                mods[i].initialized = true;
                if (mods[i].start) {
                    std::cout << "[+] Executing Start() on active render loop for: " << mods[i].filePath << std::endl << std::flush;
                    SafeCallMod([&]() { mods[i].start(); });
                }
                if (mods[i].onEnable) {
                    SafeCallMod([&]() { mods[i].onEnable(); });
                }
            }
            Render(static_cast<int>(i));
        }
    } catch (const std::exception& e) {
        std::cout << "Error in RenderAll: " << e.what() << std::endl;
    }
}

std::string ModLoader::toString(int index) {
    if (index < 0 || index >= static_cast<int>(mods.size())) {
        return "Error: Invalid mod index";
    }
    
    try {
        std::stringstream ss;
        ss << "Information" << "\n";
        ss << "Name: " << mods[index].info.name << "\n";
        ss << "Version: " << mods[index].info.version << "\n";
        ss << "Author: " << mods[index].info.author << "\n";
        ss << "Details: " << mods[index].info.description << "\n";
        return ss.str();
    } catch (const std::exception& e) {
        std::cout << "Error generating string representation for mod " << index << ": " << e.what() << std::endl;
        return "Error generating mod information";
    }
}

void ModLoader::UnloadAllMods() {
    try {
        std::cout << "Unloading all mods" << std::endl;
        
        for (size_t i = 0; i < mods.size(); i++) {
            try {
                // Disable the mod first if it's enabled
                if (mods[i].enabled && mods[i].onDisable) {
                    mods[i].onDisable();
                }
                
                // Free the library
                if (mods[i].hModule) {
                    FreeLibrary(mods[i].hModule);
                    std::cout << "Unloaded mod: " << mods[i].info.name << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "Error unloading mod " << mods[i].info.name << ": " << e.what() << std::endl;
            }
        }
        
        mods.clear();
        std::cout << "All mods unloaded" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Critical error during mod unloading: " << e.what() << std::endl;
    }
}

bool ModLoader::ReloadMod(int index) {
    if (index < 0 || index >= static_cast<int>(mods.size())) {
        std::cout << "Error: Attempted to reload mod with invalid index: " << index << std::endl;
        return false;
    }
    
    try {
        // Store the file path
        char filePath[MAX_PATH];
        if (GetModuleFileNameA(mods[index].hModule, filePath, MAX_PATH) == 0) {
            throw std::system_error(GetLastError(), std::system_category(), "Failed to get module filename");
        }
        
        std::string path = filePath;
        std::string name = mods[index].info.name;
        
        // Disable and unload the mod
        if (mods[index].enabled && mods[index].onDisable) {
            mods[index].onDisable();
        }
        
        FreeLibrary(mods[index].hModule);
        
        // Remove the mod from the vector
        mods.erase(mods.begin() + index);
        
        // Reload the mod
        std::cout << "Reloading mod: " << name << " from " << path << std::endl;
        return LoadModFromFile(path);
    } catch (const std::exception& e) {
        std::cout << "Error reloading mod at index " << index << ": " << e.what() << std::endl;
        return false;
    }
}

void ModLoader::ReloadAllMods() {
    try {
        std::cout << "Reloading all mods..." << std::endl;
        SaveModStates();
        UnloadAllMods();
        LoadMods();
        LoadModStates();
        std::cout << "All mods reloaded successfully." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error reloading all mods: " << e.what() << std::endl;
    }
}

void ModLoader::SaveModStates() {
    try {
        std::string configPath = Menu::g_path + "\\tsml_config.json";
        nlohmann::json configJson;
        std::ifstream inFile(configPath);
        if (inFile.is_open()) {
            try {
                inFile >> configJson;
            } catch (...) {}
            inFile.close();
        }

        nlohmann::json modStatesObj = nlohmann::json::object();
        for (const auto& mod : mods) {
            std::string modIdentifier = mod.info.name.empty() ? mod.filePath : mod.info.name;
            if (!modIdentifier.empty()) {
                modStatesObj[modIdentifier] = mod.enabled;
            }
        }
        configJson["modStates"] = modStatesObj;

        std::ofstream outFile(configPath);
        if (outFile.is_open()) {
            outFile << configJson.dump(4);
            outFile.close();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error saving mod states: " << e.what() << std::endl;
    }
}

void ModLoader::LoadModStates() {
    try {
        std::string configPath = Menu::g_path + "\\tsml_config.json";
        std::ifstream inFile(configPath);
        if (!inFile.is_open()) return;

        nlohmann::json configJson;
        inFile >> configJson;
        inFile.close();

        if (configJson.contains("modStates") && configJson["modStates"].is_object()) {
            auto states = configJson["modStates"];
            for (int i = 0; i < static_cast<int>(mods.size()); i++) {
                std::string modIdentifier = mods[i].info.name.empty() ? mods[i].filePath : mods[i].info.name;
                if (!modIdentifier.empty() && states.contains(modIdentifier) && states[modIdentifier].is_boolean()) {
                    bool shouldEnable = states[modIdentifier].get<bool>();
                    if (shouldEnable && !mods[i].enabled) {
                        EnableMod(i);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading mod states: " << e.what() << std::endl;
    }
}
