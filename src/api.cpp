#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <libmem.h>
#include "include/api.h"

ModApi* ModApi::instance = NULL;

ModApi& ModApi::Instance() {
    if(instance == NULL) instance = new ModApi;
    return *instance;
}

ModApi::ModApi() {
    skyBase = 0;
}

lm_module_t mod;
void ModApi::InitSkyBase() {
    int retries = 0;
    while(LM_LoadModule("Sky.exe", &mod) == 0 && retries < 100) {
        Sleep(100);
        retries++;
    }
    skyBase = mod.base;
    skySize = mod.size;
}

uintptr_t ModApi::GetSkyBase() {
	return skyBase;
}

uintptr_t ModApi::GetSkySize() {
    return skySize;
}