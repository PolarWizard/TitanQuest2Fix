/*
 * MIT License
 *
 * Copyright (c) 2025 Dominik Protasewicz
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// System includes
#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>
#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>
#include <format>
#include <numeric>
#include <numbers>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <bit>

// Local includes
#include "utils.hpp"

// Macros
#define VERSION "1.0.0"

// .yml to struct
typedef struct resolution_t {
    u32 width;
    u32 height;
    f32 aspectRatio;
} resolution_t;

typedef struct yml_t {
    std::string name;
    bool masterEnable;
    resolution_t resolution;
} yml_t;

// Globals
Utils::ModuleInfo module(GetModuleHandle(nullptr));

u32 nativeWidth = 0;
u32 nativeOffset = 0;
f32 nativeAspectRatio = (16.0f / 9.0f);
f32 widthScalingFactor = 0;

YAML::Node config = YAML::LoadFile("TitanQuest2Fix.yml");
yml_t yml;

/**
 * @brief Initializes logging for the application.
 *
 * @return void
 */
void logInit() {
    // spdlog initialisation
    auto logger = spdlog::basic_logger_mt("TitanQuest2Fix", "TitanQuest2Fix.log");
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::debug);

    // Get game name and exe path
    WCHAR exePath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(module.address, exePath, MAX_PATH);
    std::filesystem::path exeFilePath = exePath;
    module.name = exeFilePath.filename().string();

    // Log module details
    LOG("-------------------------------------");
    LOG("Compiler: {:s}", Utils::getCompilerInfo());
    LOG("Compiled: {:s} at {:s}", __DATE__, __TIME__);
    LOG("Version: {:s}", VERSION);
    LOG("Module Name: {:s}", module.name);
    LOG("Module Path: {:s}", exeFilePath.string());
    LOG("Module Addr: 0x{:x}", reinterpret_cast<u64>(module.address));
}

/**
 * @brief Reads and parses configuration settings from a YAML file.
 *
 * @return void
 */
void readYml() {
    yml.name = config["name"].as<std::string>();

    yml.masterEnable = config["masterEnable"].as<bool>();

    yml.resolution.width = config["resolution"]["width"].as<u32>();
    yml.resolution.height = config["resolution"]["height"].as<u32>();

    if (yml.resolution.width == 0 || yml.resolution.height == 0) {
        std::pair<int, int> dimensions = Utils::getDesktopDimensions();
        yml.resolution.width  = dimensions.first;
        yml.resolution.height = dimensions.second;
    }
    yml.resolution.aspectRatio = static_cast<f32>(yml.resolution.width) / static_cast<f32>(yml.resolution.height);
    nativeWidth = (16.0f / 9.0f) * static_cast<f32>(yml.resolution.height);
    nativeOffset = static_cast<f32>(yml.resolution.width - nativeWidth) / 2.0f;
    widthScalingFactor = static_cast<f32>(yml.resolution.width) / static_cast<f32>(nativeWidth);

    // Get that info!
    LOG("Name: {}", yml.name);
    LOG("MasterEnable: {}", yml.masterEnable);
    LOG("Resolution.Width: {}", yml.resolution.width);
    LOG("Resolution.Height: {}", yml.resolution.height);
    LOG("Resolution.AspectRatio: {}", yml.resolution.aspectRatio);
    LOG("Normalized Width: {}", nativeWidth);
    LOG("Normalized Offset: {}", nativeOffset);
    LOG("Width Scaling Factor: {}", widthScalingFactor);
}

void pillarBoxFix() {
    Utils::SignatureHook hook("80 3D ?? ?? ?? ?? 00    74 78    F3 0F 10 44 24 60");
    std::string patch = "01";

    bool enable = yml.masterEnable;
    LOG("Fix {}", enable ? "Enabled" : "Disabled");
    if (enable) {
        uintptr_t addr = Utils::patternScan(module.address, hook.signature);
        if (addr != 0) {
            u64 absAddr = addr;
            u64 relAddr = addr - reinterpret_cast<u64>(module.address);
            LOG("Found '{}' @ {:s}+{:x}", hook.signature, module.name, relAddr);
            u64 patchAbsAddr = absAddr + 6;
            u64 patchRelAddr = relAddr + 6;
            Utils::patch(patchAbsAddr, patch);
            LOG("Patched '{}' @ {:s}+{:x}", patch, module.name, patchRelAddr);
        } else {
            LOG("Failed to find '{}' @ {:s}", hook.signature, module.name);
        }
    }
}

/**
 * @brief This function serves as the entry point for the DLL. It performs the following tasks:
 * 1. Initializes the logging system.
 * 2. Reads the configuration from a YAML file.
 * 3. Applies a center UI fix.
 *
 * @param lpParameter Unused parameter.
 * @return Always returns TRUE to indicate successful execution.
 */
DWORD WINAPI Main(void* lpParameter) {
    logInit();
    readYml();
    pillarBoxFix();
    return true;
}

/**
 * @brief Entry point for a DLL, called by the system when the DLL is loaded or unloaded.
 *
 * This function handles various events related to the DLL's lifetime and performs actions
 * based on the reason for the call. Specifically, it creates a new thread when the DLL is
 * attached to a process.
 *
 * @details
 * The `DllMain` function is called by the system when the DLL is loaded or unloaded. It handles
 * different reasons for the call specified by `ul_reason_for_call`. In this implementation:
 *
 * - **DLL_PROCESS_ATTACH**: When the DLL is loaded into the address space of a process, it
 *   creates a new thread to run the `Main` function. The thread priority is set to the highest,
 *   and the thread handle is closed after creation.
 *
 * - **DLL_THREAD_ATTACH**: Called when a new thread is created in the process. No action is taken
 *   in this implementation.
 *
 * - **DLL_THREAD_DETACH**: Called when a thread exits cleanly. No action is taken in this implementation.
 *
 * - **DLL_PROCESS_DETACH**: Called when the DLL is unloaded from the address space of a process.
 *   No action is taken in this implementation.
 *
 * @param hModule Handle to the DLL module. This parameter is used to identify the DLL.
 * @param ul_reason_for_call Indicates the reason for the call (e.g., process attach, thread attach).
 * @param lpReserved Reserved for future use. This parameter is typically NULL.
 * @return BOOL Always returns TRUE to indicate successful execution.
 */
BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
) {
    HANDLE mainHandle;
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        Sleep(5000);
        mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0);
        if (mainHandle)
        {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
