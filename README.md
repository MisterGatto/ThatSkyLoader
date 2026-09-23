<p align="center">
  <img src="https://img2.storyblok.com/fit-in/0x300/filters:format(webp)/f/108104/368x415/436d2e239c/sky-logo-white.png" alt="Sky: Children of the Light" width="300"/>
</p>

# ThatSkyLoader made by XeTrinityz

> **Project Notice & History:**  
> This project is a revived and modernized fork of the mod loader for **Sky: Children of the Light** on PC.  
> - The original project was created by **lukas** (`sml-pc`, who has since removed the repository).  
> - It was later adapted and published as **ThatSkyLoader** by **XeTrinityz**, who has since removed the repository.  
> - **This repository** preserves, fixes, resolving numerous crashes and issues to ensure full compatibility with the **latest PC (Steam) releases of Sky: Children of the Light**.

---

## What's New

- Fixed Vulkan bugs.

---

## Features

- **Vulkan Layer Injection**: Injects seamlessly via standard Windows DLL proxying (`powrprof.dll`) without modifying or altering game files.
- **In-Game Menu**: Clean Dear ImGui interface over the game renderer (press **F5** to toggle).
- **Dynamic Mod Control**: Enable, disable, and reload mods at runtime without restarting the game.
- **Direct Rendering Hook**: Allows mods to draw custom ImGui elements via their exported `Render()` hook.
- **Persistent Configuration**: Mod states, selected fonts, and hotkeys are preserved across launches in `tsml_config.json`.

---

## Installation

1. Download or build `powrprof.dll`.
2. Navigate to your Sky installation directory:
 ```Directory Example```
 
   ```
   D:\SteamLibrary\steamapps\common\Sky Children of the Light\
   ```
3. Place `powrprof.dll` into the game directory (alongside `Sky.exe`).
4. Create a folder named `mods` in the same directory if it does not already exist.
5. Place your mod DLL files into the `mods` folder.
6. Launch Sky through Steam. Press **F5** at any time to open the mod menu.

---

## In-Game Controls & Configuration

- **Toggle Menu**: Press **F5** (default). You can change this hotkey in the *Settings* section of the menu.
- **Search Mods**: Type in the *Filter* box to find mods by name.
- **Reload All**: Unloads and reloads all mods currently present in the `mods/` directory.
- **Rescan Folder**: Detects newly added or removed mod files on the fly (need to restart  the game to activate them).
- **Configuration File**: Settings and individual mod enable/disable states are automatically saved to `tsml_config.json`.

---

## Building from Source

### Prerequisites

- Windows 10 / 11 (x64)
- Visual Studio 2022 with Desktop C++ Development tools
- CMake 3.20 or newer
- x64 Native Tools Command Prompt for VS 2022

### Build Instructions

1. Open the **x64 Native Tools Command Prompt for VS 2022**.
2. Clone this repository:
   ```sh
   git clone https://github.com/XeTrinityz/ThatSkyLoader.git
   cd ThatSkyLoader
   ```
3. Create a build directory and compile using NMake:
   ```cmd
   mkdir build
   cd build
   cmake .. -G "NMake Makefiles" -D CMAKE_BUILD_TYPE="Release"
   nmake
   ```
4. Upon successful build, the compiled DLL will be located at:
   - `build/powrprof.dll`
   - `lib/release/powrprof.lib`

---

## Developing Mods

Mods for ThatSkyLoader are standard x64 Windows dynamic link libraries (`.dll`) that export any of the following lifecycle functions:

```cpp
#include <windows.h>
#include <string>

struct ModInfo {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
};

extern "C" {
    __declspec(dllexport) void GetModInfo(ModInfo& info) {
        info.name = "Sample Mod";
        info.version = "1.0.0";
        info.author = "YourName";
        info.description = "A demonstration mod for Sky.";
    }

    __declspec(dllexport) void Start() {
        // Called when the mod is first initialized
    }

    __declspec(dllexport) void onEnable() {
        // Called when the mod is toggled ON in the menu
    }

    __declspec(dllexport) void onDisable() {
        // Called when the mod is toggled OFF in the menu
    }

    __declspec(dllexport) void Render() {
        // Called every frame inside the ImGui render loop
        // You can draw custom ImGui windows and widgets here
    }
}
```

---

## Project Structure

```
ThatSkyLoader/
├── CMakeLists.txt              # CMake build configuration
├── README.md                   # Project documentation
├── include/                    # Headers & dependencies
│   ├── imgui/                  # Dear ImGui library headers
│   ├── libmem/                 # Memory manipulation library headers
│   └── vulkan/                 # Vulkan SDK headers
├── src/                        # Source files
│   ├── layer.cpp               # Vulkan layer interception & ImGui renderer hook
│   ├── mod_loader.cpp          # Mod discovery, dynamic loading, and lifecycle dispatch
│   ├── menu.cpp                # Dear ImGui UI, font management, and config storage
│   ├── main.cpp                # DLL entry point & registry/environment hooks
│   └── imgui_*.cpp             # ImGui implementation files
└── lib/                        # Static and import libraries
```

---

## Credits & Acknowledgments

- **Lukas**: Original creator of `sml-pc`.
- **XeTrinityz**: Creator of `ThatSkyLoader`.
- **MrGatto**: Fixed bugs and Vulkan crashes.
- **Dear ImGui** by ocornut for the graphical interface.

---

<p align="center">
  Made with ❤️ for the Sky: Children of the Light community.
</p>
