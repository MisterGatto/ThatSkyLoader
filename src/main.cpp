#include <chrono>
#include <string>
#include <windows.h>
#include <stdio.h>
#include <vulkan/vulkan.h>
#include <thread>
#include <dxgi.h>
#include <libmem.h>
#include <imgui.h>
#include <winreg.h>
#include <winuser.h>
#include <iostream>
#include <fstream>
#include <streambuf>
#include <mutex>
#include <atomic>
#include <tlhelp32.h>
#include <random>
#include <sstream>
#include <shellapi.h>
#include <psapi.h>
#include <vector>
#include <iomanip>
#include "include/api.h"
#include "include/layer.h"
#include "include/menu.hpp"
#include "include/mod_loader.h"
#include "include/json.hpp"

HMODULE dllHandle = nullptr;

BOOLEAN(*o_GetPwrCapabilities)(PSYSTEM_POWER_CAPABILITIES) = nullptr;
NTSTATUS(*o_CallNtPowerInformation)(POWER_INFORMATION_LEVEL, PVOID, ULONG, PVOID, ULONG) = nullptr;
POWER_PLATFORM_ROLE(*o_PowerDeterminePlatformRole)() = nullptr;
DWORD(*o_PowerReadACValue)(HKEY, const GUID*, const GUID*, const GUID*, PULONG, LPBYTE, LPDWORD) = nullptr;
DWORD(*o_PowerReadDCValue)(HKEY, const GUID*, const GUID*, const GUID*, PULONG, LPBYTE, LPDWORD) = nullptr;
DWORD(*o_PowerWriteACValue)(HKEY, const GUID*, const GUID*, const GUID*, ULONG, DWORD, LPBYTE, DWORD) = nullptr;
DWORD(*o_PowerWriteDCValue)(HKEY, const GUID*, const GUID*, const GUID*, ULONG, DWORD, LPBYTE, DWORD) = nullptr;
DWORD(*o_PowerSetActiveScheme)(HKEY, const GUID*) = nullptr;
BOOLEAN(*o_SetSuspendState)(BOOLEAN, BOOLEAN, BOOLEAN) = nullptr;
BOOLEAN(*o_IsPwrSuspendAllowed)() = nullptr;
BOOLEAN(*o_IsPwrHibernateAllowed)() = nullptr;
BOOLEAN(*o_IsPwrShutdownAllowed)() = nullptr;

extern "C"
__declspec(dllexport) BOOLEAN __stdcall GetPwrCapabilities(PSYSTEM_POWER_CAPABILITIES lpspc) {
    if (o_GetPwrCapabilities) return o_GetPwrCapabilities(lpspc);
    return FALSE;
}

extern "C"
__declspec(dllexport) NTSTATUS __stdcall CallNtPowerInformation(POWER_INFORMATION_LEVEL InformationLevel, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength) {
    if (o_CallNtPowerInformation) return o_CallNtPowerInformation(InformationLevel, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);
    return ((NTSTATUS)0xC0000002L);
}

extern "C"
__declspec(dllexport) POWER_PLATFORM_ROLE PowerDeterminePlatformRole() {
    if (o_PowerDeterminePlatformRole) return o_PowerDeterminePlatformRole();
    return PlatformRoleUnspecified;
}

extern "C"
__declspec(dllexport) DWORD __stdcall PowerReadACValue(HKEY RootPowerKey, const GUID* SchemeGuid, const GUID* SubGroupOfPowerSettingsGuid, const GUID* PowerSettingGuid, PULONG Type, LPBYTE Buffer, LPDWORD BufferSize) {
    if (o_PowerReadACValue) return o_PowerReadACValue(RootPowerKey, SchemeGuid, SubGroupOfPowerSettingsGuid, PowerSettingGuid, Type, Buffer, BufferSize);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

extern "C"
__declspec(dllexport) DWORD __stdcall PowerReadDCValue(HKEY RootPowerKey, const GUID* SchemeGuid, const GUID* SubGroupOfPowerSettingsGuid, const GUID* PowerSettingGuid, PULONG Type, LPBYTE Buffer, LPDWORD BufferSize) {
    if (o_PowerReadDCValue) return o_PowerReadDCValue(RootPowerKey, SchemeGuid, SubGroupOfPowerSettingsGuid, PowerSettingGuid, Type, Buffer, BufferSize);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

extern "C"
__declspec(dllexport) DWORD __stdcall PowerWriteACValue(HKEY RootPowerKey, const GUID* SchemeGuid, const GUID* SubGroupOfPowerSettingsGuid, const GUID* PowerSettingGuid, ULONG Type, DWORD BufferSize, LPBYTE Buffer, DWORD BufferLength) {
    if (o_PowerWriteACValue) return o_PowerWriteACValue(RootPowerKey, SchemeGuid, SubGroupOfPowerSettingsGuid, PowerSettingGuid, Type, BufferSize, Buffer, BufferLength);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

extern "C"
__declspec(dllexport) DWORD __stdcall PowerWriteDCValue(HKEY RootPowerKey, const GUID* SchemeGuid, const GUID* SubGroupOfPowerSettingsGuid, const GUID* PowerSettingGuid, ULONG Type, DWORD BufferSize, LPBYTE Buffer, DWORD BufferLength) {
    if (o_PowerWriteDCValue) return o_PowerWriteDCValue(RootPowerKey, SchemeGuid, SubGroupOfPowerSettingsGuid, PowerSettingGuid, Type, BufferSize, Buffer, BufferLength);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

extern "C"
__declspec(dllexport) DWORD __stdcall PowerSetActiveScheme(HKEY UserRootPowerKey, const GUID* SchemeGuid) {
    if (o_PowerSetActiveScheme) return o_PowerSetActiveScheme(UserRootPowerKey, SchemeGuid);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

extern "C"
__declspec(dllexport) BOOLEAN __stdcall SetSuspendState(BOOLEAN bHibernate, BOOLEAN bForceCritical, BOOLEAN bDisableWakeEvent) {
    if (o_SetSuspendState) return o_SetSuspendState(bHibernate, bForceCritical, bDisableWakeEvent);
    return FALSE;
}

extern "C"
__declspec(dllexport) BOOLEAN __stdcall IsPwrSuspendAllowed() {
    if (o_IsPwrSuspendAllowed) return o_IsPwrSuspendAllowed();
    return FALSE;
}

extern "C"
__declspec(dllexport) BOOLEAN __stdcall IsPwrHibernateAllowed() {
    if (o_IsPwrHibernateAllowed) return o_IsPwrHibernateAllowed();
    return FALSE;
}

extern "C"
__declspec(dllexport) BOOLEAN __stdcall IsPwrShutdownAllowed() {
    if (o_IsPwrShutdownAllowed) return o_IsPwrShutdownAllowed();
    return FALSE;
}

// SML Log file
std::ofstream SML_Log;

// Custom Streambuf for logging to both file and console
class StreamBuf : public std::streambuf {
public:
    StreamBuf(std::streambuf* buf, const std::string& prefix)
        : originalBuf(buf), logPrefix(prefix) {
        // Reserve space to reduce reallocations
        buffer.reserve(256);
    }

protected:
    virtual int overflow(int c) override {
        if (c != EOF) {
            if (c == '\n') {
                // Write to log file with prefix
                SML_Log << logPrefix << buffer << std::endl;
                // Ensure data is written immediately
                SML_Log.flush();
                // Clear buffer efficiently (maintains capacity)
                buffer.clear();
            }
            else {
                // Append character to buffer
                buffer += static_cast<char>(c);
            }
        }
        // Always forward to original buffer
        return originalBuf->sputc(c);
    }

    // Implement sync for better control of buffer flushing
    virtual int sync() override {
        if (!buffer.empty()) {
            SML_Log << logPrefix << buffer << std::flush;
            buffer.clear();
        }
        return originalBuf->pubsync();
    }

private:
    std::streambuf* originalBuf;
    std::string logPrefix;
    std::string buffer;
};

void print(const char* format, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    std::cout << std::string(buffer) << std::flush;
}

void InitLogger() {
    SML_Log.open("TSML.log", std::ios::out | std::ios::app);

    // Redirect std::cout, and std::cerr to SML.log
    static StreamBuf coutBuf(std::cout.rdbuf(), "[OUTPUT] ");
    std::cout.rdbuf(&coutBuf);

    static StreamBuf cerrBuf(std::cerr.rdbuf(), "[ERROR] ");
    std::cerr.rdbuf(&cerrBuf);
}

void InitConsole() {
    FreeConsole();
    AllocConsole();
    SetConsoleTitle("TSML Console");

    if (IsValidCodePage(CP_UTF8)) {
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
    }

    auto hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleMode(hStdout, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    // Disable Ctrl+C handling
    SetConsoleCtrlHandler(NULL, TRUE);

    CONSOLE_FONT_INFOEX cfi;
    cfi.cbSize = sizeof(cfi);
    GetCurrentConsoleFontEx(hStdout, FALSE, &cfi);

    // Change to a more readable font if user has one of the default eyesore fonts
    if (wcscmp(cfi.FaceName, L"Terminal") == 0 || wcscmp(cfi.FaceName, L"Courier New") || (cfi.FontFamily & TMPF_VECTOR) == 0) {
        cfi.cbSize = sizeof(cfi);
        cfi.nFont = 0;
        cfi.dwFontSize.X = 0;
        cfi.dwFontSize.Y = 14;
        cfi.FontFamily = FF_MODERN | TMPF_VECTOR | TMPF_TRUETYPE;
        cfi.FontWeight = FW_NORMAL;
        wcscpy_s(cfi.FaceName, L"Lucida Console");
        SetCurrentConsoleFontEx(hStdout, FALSE, &cfi);
    }

    FILE* file;
    freopen_s(&file, "CONOUT$", "w", stdout);
    freopen_s(&file, "CONOUT$", "w", stderr);
    freopen_s(&file, "CONIN$", "r", stdin);

    fflush(stdout);
    fflush(stderr);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static WNDPROC oWndProc;
LRESULT WINAPI HookWndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if ((uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) && (wParam == VK_F5 || (Menu::g_toggleKey && wParam == Menu::g_toggleKey))) {
        Menu::bShowMenu = !Menu::bShowMenu;
        print("Menu toggled: %s\n", Menu::bShowMenu ? "ON" : "OFF");
        return 0;
    }
    if (Menu::bShowMenu) {
        try {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
                return 1;
            }
            if (ImGui::GetCurrentContext() != nullptr) {
                ImGuiIO& io = ImGui::GetIO();
                if (io.WantCaptureMouse &&
                    (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP ||
                     uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP ||
                     uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP ||
                     uMsg == WM_MOUSEWHEEL || uMsg == WM_MOUSEMOVE)) {
                    return 0;
                }
                if (io.WantCaptureKeyboard && (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_CHAR)) {
                    return 0;
                }
            }
        } catch (...) {}
    }
    if (oWndProc && oWndProc != HookWndProc) {
        return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void HookWindowProc(HWND window) {
    if (!window) return;
    WNDPROC oldProc = (WNDPROC)GetWindowLongPtr(window, GWLP_WNDPROC);
    if (oldProc && oldProc != HookWndProc) {
        oWndProc = oldProc;
        SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)HookWndProc);
        print("WndProc hooked successfully on render thread\n");
    }
}

DWORD WINAPI hook_thread(PVOID lParam) {
    HWND window = nullptr;
    print("Searching for Sky Window\n");

    int retries = 0;
    constexpr int max_retries = 300;
    while (!window && retries < max_retries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!window) {
            window = FindWindowA("TgcMainWindow", NULL);
        }
        if (!window) {
            HWND hwndTop = GetTopWindow(GetDesktopWindow());
            while (hwndTop) {
                DWORD pid = 0;
                GetWindowThreadProcessId(hwndTop, &pid);
                if (pid == GetCurrentProcessId()) {
                    char className[256] = {0};
                    GetClassNameA(hwndTop, className, sizeof(className));
                    if (strstr(className, "TgcMainWindow") || strstr(className, "Sky")) {
                        window = hwndTop;
                        break;
                    }
                }
                hwndTop = GetNextWindow(hwndTop, GW_HWNDNEXT);
            }
        }
        retries++;
    }

    if (window) {
        print("Sky window found!\n");
        layer::setup(window);
        HookWindowProc(window);
        ModApi::Instance().InitSkyBase();
        ModLoader::LoadMods();
        ModLoader::LoadModStates();
    } else {
        print("Warning: Sky window search timed out, loading mods directly\n");
        ModApi::Instance().InitSkyBase();
        ModLoader::LoadMods();
        ModLoader::LoadModStates();
    }
    return EXIT_SUCCESS;
}

void terminateCrashpadHandler() {
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (snapshot != INVALID_HANDLE_VALUE && Process32First(snapshot, &entry) == TRUE) {
        do {
#ifdef UNICODE
            bool isCrashpad = (_wcsicmp(entry.szExeFile, L"crashpad_handler.exe") == 0);
#else
            bool isCrashpad = (_stricmp(entry.szExeFile, "crashpad_handler.exe") == 0);
#endif
            if (isCrashpad) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);

                if (hProcess != NULL) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                    print("Detected and closed crashpad_handler.exe\n");
                }
            }
        } while (Process32Next(snapshot, &entry) == TRUE);
        CloseHandle(snapshot);
    }
}

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif

std::wstring GetKeyPathFromKKEY(HKEY key)
{
    std::wstring keyPath;
    if (key != NULL)
    {
        static HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
        if (hNtdll != NULL) {
            typedef DWORD(__stdcall* NtQueryKeyType)(
                HANDLE  KeyHandle,
                int KeyInformationClass,
                PVOID  KeyInformation,
                ULONG  Length,
                PULONG  ResultLength);

            static NtQueryKeyType pfnNtQueryKey = reinterpret_cast<NtQueryKeyType>(GetProcAddress(hNtdll, "NtQueryKey"));

            if (pfnNtQueryKey != NULL) {
                DWORD size = 0;
                DWORD result = pfnNtQueryKey(key, 3, nullptr, 0, &size);
                if (result == STATUS_BUFFER_TOO_SMALL && size > 0)
                {
                    size_t count = (size / sizeof(wchar_t)) + 2;
                    wchar_t* buffer = new (std::nothrow) wchar_t[count];
                    if (buffer != NULL)
                    {
                        memset(buffer, 0, count * sizeof(wchar_t));
                        result = pfnNtQueryKey(key, 3, buffer, size, &size);
                        if (result == STATUS_SUCCESS)
                        {
                            size_t actualLen = size / sizeof(wchar_t);
                            if (actualLen > 2) {
                                buffer[actualLen] = L'\0';
                                keyPath = std::wstring(buffer + 2);
                            }
                        }
                        delete[] buffer;
                    }
                }
            }
        }
    }
    return keyPath;
}

#undef STATUS_BUFFER_TOO_SMALL
#undef STATUS_SUCCESS

typedef LSTATUS(__stdcall* PFN_RegEnumValueA)(HKEY hKey, DWORD dwIndex, LPSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
PFN_RegEnumValueA oRegEnumValueA = nullptr;

LSTATUS hkRegEnumValueA(HKEY hKey, DWORD dwIndex, LPSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData) {
    if (!oRegEnumValueA) {
        return ERROR_INVALID_FUNCTION;
    }

    std::wstring path = GetKeyPathFromKKEY(hKey);

    std::string name = Menu::g_path + "\\tsml_config.json";
    std::ifstream file(name);
    if (!file.is_open()) {
        Menu::EnsureConfigFileExists();
    } else {
        file.close();
    }

    if (wcscmp(path.c_str(), L"\\REGISTRY\\MACHINE\\SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers") == 0 ||
        wcsstr(path.c_str(), L"Khronos\\Vulkan\\ImplicitLayers") != nullptr) {
        if (dwIndex == 0) {
            if (lpcchValueName != nullptr && *lpcchValueName <= name.size()) {
                *lpcchValueName = static_cast<DWORD>(name.size() + 1);
                return ERROR_MORE_DATA;
            }
            if (lpValueName != nullptr) {
                memcpy(lpValueName, name.c_str(), name.size() + 1);
            }
            if (lpcchValueName != nullptr) {
                *lpcchValueName = static_cast<DWORD>(name.size());
            }
            if (lpType != nullptr) {
                *lpType = REG_DWORD;
            }
            if (lpData != nullptr && lpcbData != nullptr && *lpcbData >= sizeof(DWORD)) {
                *reinterpret_cast<DWORD*>(lpData) = 0;
                *lpcbData = sizeof(DWORD);
            } else if (lpcbData != nullptr) {
                *lpcbData = sizeof(DWORD);
            }
            return ERROR_SUCCESS;
        } else {
            return oRegEnumValueA(hKey, dwIndex - 1, lpValueName, lpcchValueName, lpReserved, lpType, lpData, lpcbData);
        }
    }

    return oRegEnumValueA(hKey, dwIndex, lpValueName, lpcchValueName, lpReserved, lpType, lpData, lpcbData);
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    
    DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
    PVOID addr = pExceptionInfo->ExceptionRecord->ExceptionAddress;
    
    std::cerr << "\n==========================================" << std::endl;
    std::cerr << "[CRASH DETECTED] Exception Code: 0x" << std::hex << code << std::dec << std::endl;
    std::cerr << "[CRASH DETECTED] Fault Address: 0x" << std::hex << (uintptr_t)addr << std::dec << std::endl;

    if (code == EXCEPTION_ACCESS_VIOLATION && pExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR type = pExceptionInfo->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR targetAddr = pExceptionInfo->ExceptionRecord->ExceptionInformation[1];
        std::cerr << "[CRASH DETECTED] Access Violation: " << (type == 0 ? "Read from" : (type == 1 ? "Write to" : "Execute at"))
                  << " 0x" << std::hex << targetAddr << std::dec << std::endl;
    }

    HMODULE hMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &hMod)) {
        char modPath[MAX_PATH] = {0};
        GetModuleFileNameA(hMod, modPath, MAX_PATH);
        uintptr_t offset = (uintptr_t)addr - (uintptr_t)hMod;
        std::cerr << "[CRASH DETECTED] Module: " << modPath << " (Offset: 0x" << std::hex << offset << std::dec << ")" << std::endl;
    }
    std::cerr << "==========================================\n" << std::endl;
    SML_Log.flush();
    
    return EXCEPTION_CONTINUE_SEARCH;
}

bool IsSkyProcess() {
    char processPath[MAX_PATH] = {0};
    if (GetModuleFileNameA(NULL, processPath, MAX_PATH) > 0) {
        std::string path(processPath);
        std::string filename = path.substr(path.find_last_of("\\/") + 1);
        std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
        if (filename.find("crashpad") != std::string::npos) {
            return false;
        }
    }
    return true;
}

void onAttach() {
    SetUnhandledExceptionFilter(CrashHandler);

    dllHandle = LoadLibraryA("C:\\Windows\\System32\\powrprof.dll");
    if (dllHandle == NULL) dllHandle = LoadLibraryA("C:\\Windows\\System32\\POWRPROF.dll");

    if (dllHandle != NULL) {
        o_GetPwrCapabilities = (decltype(o_GetPwrCapabilities))GetProcAddress(dllHandle, "GetPwrCapabilities");
        o_CallNtPowerInformation = (decltype(o_CallNtPowerInformation))GetProcAddress(dllHandle, "CallNtPowerInformation");
        o_PowerDeterminePlatformRole = (decltype(o_PowerDeterminePlatformRole))GetProcAddress(dllHandle, "PowerDeterminePlatformRole");
        o_PowerReadACValue = (decltype(o_PowerReadACValue))GetProcAddress(dllHandle, "PowerReadACValue");
        o_PowerReadDCValue = (decltype(o_PowerReadDCValue))GetProcAddress(dllHandle, "PowerReadDCValue");
        o_PowerWriteACValue = (decltype(o_PowerWriteACValue))GetProcAddress(dllHandle, "PowerWriteACValue");
        o_PowerWriteDCValue = (decltype(o_PowerWriteDCValue))GetProcAddress(dllHandle, "PowerWriteDCValue");
        o_PowerSetActiveScheme = (decltype(o_PowerSetActiveScheme))GetProcAddress(dllHandle, "PowerSetActiveScheme");
        o_SetSuspendState = (decltype(o_SetSuspendState))GetProcAddress(dllHandle, "SetSuspendState");
        o_IsPwrSuspendAllowed = (decltype(o_IsPwrSuspendAllowed))GetProcAddress(dllHandle, "IsPwrSuspendAllowed");
        o_IsPwrHibernateAllowed = (decltype(o_IsPwrHibernateAllowed))GetProcAddress(dllHandle, "IsPwrHibernateAllowed");
        o_IsPwrShutdownAllowed = (decltype(o_IsPwrShutdownAllowed))GetProcAddress(dllHandle, "IsPwrShutdownAllowed");

        if (o_GetPwrCapabilities == nullptr || o_CallNtPowerInformation == nullptr || o_PowerDeterminePlatformRole == nullptr) {
            print("Warning: Could not locate standard symbols in powrprof.dll\n");
        }
    }
    else print("Failed to load POWRPROF.dll\n");

    if (!IsSkyProcess()) {
        return;
    }

    terminateCrashpadHandler();

    InitConsole();
    std::remove("TSML.log");
    InitLogger();
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring ws(path);
    std::string _path(ws.begin(), ws.end());
    Menu::g_path = _path.substr(0, _path.find_last_of("\\/"));

    Menu::EnsureConfigFileExists();
    Menu::LoadConfig();

    HMODULE handle = LoadLibraryA("advapi32.dll");
    if (handle != NULL) {
        lm_address_t fnRegEnumValue = (lm_address_t)GetProcAddress(handle, "RegEnumValueA");
        if (fnRegEnumValue != 0) {
            if (LM_HookCode(fnRegEnumValue, (lm_address_t)&hkRegEnumValueA, (lm_address_t*)&oRegEnumValueA)) {
                print("Hooked fnRegEnumValue successfully\n");
            } else {
                print("Failed to hook fnRegEnumValue\n");
            }
        }
    }

    CreateThread(NULL, 0, hook_thread, nullptr, 0, NULL);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    DisableThreadLibraryCalls(hinstDLL);

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        onAttach();
        break;
    case DLL_PROCESS_DETACH:
        break;
    }

    return TRUE;
}