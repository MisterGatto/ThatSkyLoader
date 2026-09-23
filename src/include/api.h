#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(__CYGWIN__)
    #define MOD_API __declspec(dllexport)
#else   
    #define MOD_API __attribute__((visibility("default")))
#endif

typedef struct ModInfo {
    std::string name;
    std::string author;
    std::string description;
    std::string version;
}ModInfo;

class MOD_API ModApi {
protected:
    static ModApi *instance;
    uintptr_t skyBase;
    size_t skySize;

public:
    static ModApi& Instance();

    ModApi();

    void InitSkyBase();

    uintptr_t GetSkyBase();
    uintptr_t GetSkySize();
};