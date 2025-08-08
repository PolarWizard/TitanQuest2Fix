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
#define VERSION "1.2.1"

// .yml to struct
typedef struct resolution_t {
    u32 width;
    u32 height;
    f32 aspectRatio;
} resolution_t;

typedef struct pillarbox_t {
    bool enable;
} pillarbox_t;

typedef struct fix_t {
    pillarbox_t pillarbox;
} fix_t;

typedef struct fov_t {
    bool enable;
    f32 value;
} fov_t;

typedef struct hud_t {
    bool enable;
} hud_t;

typedef struct feature_t {
    fov_t fov;
    hud_t hud;
} feature_t;

typedef struct yml_t {
    std::string name;
    bool masterEnable;
    resolution_t resolution;
    fix_t fixes;
    feature_t features;
} yml_t;

// Globals
namespace {
    Utils::ModuleInfo module(GetModuleHandle(nullptr));

    u32 nativeWidth = 0;
    u32 nativeOffset = 0;
    f32 nativeAspectRatio = (16.0f / 9.0f);
    f32 widthScalingFactor = 0;

    YAML::Node config = YAML::LoadFile("TitanQuest2Fix.yml");
    yml_t yml;
}

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

    yml.fixes.pillarbox.enable = config["fixes"]["pillarbox"]["enable"].as<bool>();

    yml.features.fov.enable = config["features"]["fov"]["enable"].as<bool>();
    yml.features.fov.value = config["features"]["fov"]["value"].as<f32>();

    yml.features.hud.enable = config["features"]["hud"]["enable"].as<bool>();

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
    LOG("Fixes.Pillarbox.Enable: {}", yml.fixes.pillarbox.enable);
    LOG("Features.FOV.Enable: {}", yml.features.fov.enable);
    LOG("Features.FOV.Value: {}", yml.features.fov.value);
    LOG("Features.HUD.Enable: {}", yml.features.hud.enable);
}

/**
 * @brief Removes the pillarbox when exceeding 21:9 resolution.
 *
 * @details
 * The game is programmed to pillarbox when resolutions exceed 21:9. Interestingly this only happens when
 * your in game, the main menu for example is not effected by this.
 *
 * How was this found?
 * Unreal Engine 5 is a very capable engine, so pillarboxing is something that the developer themselves have
 * put in, it is not a limitation of the engine, as there are games that do not pillarbox at 32:9.
 *
 * Now that we know this is something that the devs put in, that means that the engine has an option to enable
 * it and at what aspect ratio it should be taking effect at. This will be our strategy for attack.
 *
 * We first load up the game in standard 16:9 resolution, and using cheat engine start scanning for the ratio
 * calculation: 16/9 = 1.7777777778. To my surprise there really aren't that many references for that ratio.
 * With the complete now we can crank up the resolution to 32:9 or really any resolution that exceeds 21:9,
 * get back in game from the options menu and start inspecting the what values in the first scan have changed.
 * We see that most of the scan values stay at 1.78, but some have changed to 3.56 (since I changed the
 * resolution to 32:9, lower aspect ratios your results will be different here), but we also see a couple
 * 2.44 values, which is very close to the ratio calculation for 21:9 = 2.33. These are the numbers that will
 * be investigated.
 *
 * Firstly I tried changing them to see if that anything and to no surprise that did nothing so they are
 * constantly being updated by the game. Ok, so next thing to do is figure out who accesses the memory
 * locations those nunbers are stored.
 *
 * After analyzing who accesses those memory locations we learn that most of the 2.44 values the code is
 * doing basic copy and pasting of the value to various, I assume, data structures littered and used
 * throughout the game, and none of the code nearby to the access has anything of interest happening.
 *
 * There is however one location where a floating point register is being used and that happens here:
 * TQ2-Win64-Shipping.exe+2393BDD - F3 0F11 B0 B0020000   - movss [rax+000002B0],xmm6
 *
 * The surrounding code also makes use trig functions which are typically used when it comes to viewport
 * calculations so we are on the right track here. Earlier in the function we see some suspicious code:
 * 1 - TQ2-Win64-Shipping.exe+2393BB1 - 0F2F CA               - comiss xmm1,xmm2
 * 2 - TQ2-Win64-Shipping.exe+2393BB4 - 72 07                 - jb TQ2-Win64-Shipping.exe+2393BBD
 * 3 - TQ2-Win64-Shipping.exe+2393BB6 - 0F28 F1               - movaps xmm6,xmm1
 * 4 - TQ2-Win64-Shipping.exe+2393BB9 - F3 0F5D F3            - minss xmm6,xmm3
 * 5 - TQ2-Win64-Shipping.exe+2393BBD - 0F2E F1               - ucomiss xmm6,xmm1
 * 6 - TQ2-Win64-Shipping.exe+2393BC0 - 0F84 81000000         - je TQ2-Win64-Shipping.exe+2393C47
 * 7 - TQ2-Win64-Shipping.exe+2393BC6 - 80 3D F868ED05 00     - cmp byte ptr [TQ2-Win64-Shipping.exe+826A4C5],00
 *
 * Breakpointing and inspecting the xmm registers we see the following:
 * xmm1 = 3.56 (I have selected 32:9 resolution, different aspect ratios will result in different values here)
 * xmm2 = 1.33 (4:3 aspect ratio)
 * xmm3 = 2.44 (~21:9 aspect ratio)
 *
 * The game checks if the screen is firstly narrower than 4:3 and if it's not then it compares your screen
 * aspect ratio stored to the game's hardcoded cutoff of 21:9, and places the lower value into xmm6. Then
 * if the chosen value in xmm6 is less than the value in xmm1 then we are safe and dont need to do calculations
 * to apply a pillarbox to the screen and the jump on line 6 and render the game to fit the whole screen. If
 * the chosen value in xmm6 is greater than the value in xmm1 then we fall through to the most important line
 * in the snippet, line 7. This is the line that is specifically tells the game if a pillarbox should be
 * applied or not. The value at TQ2-Win64-Shipping.exe+826A4C5 is most likely the option that the engine exposes
 * to the developers of whether or not a pillarbox should be applied and this line is the check for that option.
 * The value at that location is 1 which causes this check to succeed meaning that a pillarbox will be applied.
 *
 * This is where I have chosen to implement this fix to remove the pillarbox. There are other things you can
 * do to also remove the pillarbox, but this is my approach to the problem. So I have patched that line:
 * NEW - TQ2-Win64-Shipping.exe+2393BC6 - 80 3D F868ED05 01     - cmp byte ptr [TQ2-Win64-Shipping.exe+826A4C5],01
 * OLD - TQ2-Win64-Shipping.exe+2393BC6 - 80 3D F868ED05 00     - cmp byte ptr [TQ2-Win64-Shipping.exe+826A4C5],00
 *
 * Now we check if the memory location at TQ2-Win64-Shipping.exe+826A4C5 is set to 1 and not 0. And just like that
 * pillarbox is gone.
 *
 * Extra details for nerds:
 * This is the code block we enter if the game needs to apply a pillarbox:
 * 1  - TQ2-Win64-Shipping.exe+2393BCF - F3 0F10 44 24 60      - movss xmm0,[rsp+60]
 * 2  - TQ2-Win64-Shipping.exe+2393BD5 - F3 0F5F 05 73DFD203   - maxss xmm0,[TQ2-Win64-Shipping.exe+60C1B50]
 * 3  - TQ2-Win64-Shipping.exe+2393BDD - F3 0F11 B0 B0020000   - movss [rax+000002B0],xmm6
 * 4  - TQ2-Win64-Shipping.exe+2393BE5 - 48 8B 81 E0090000     - mov rax,[rcx+000009E0]
 * 5  - TQ2-Win64-Shipping.exe+2393BEC - 48 89 7C 24 40        - mov [rsp+40],rdi
 * 6  - TQ2-Win64-Shipping.exe+2393BF1 - F3 0F59 05 BBD2E103   - mulss xmm0,[TQ2-Win64-Shipping.exe+61B0EB4]
 * 7  - TQ2-Win64-Shipping.exe+2393BF9 - 80 88 B4020000 01     - or byte ptr [rax+000002B4],01
 * 8  - TQ2-Win64-Shipping.exe+2393C00 - 48 8B 99 E0090000     - mov rbx,[rcx+000009E0]
 * 9  - TQ2-Win64-Shipping.exe+2393C07 - 48 8B 3B              - mov rdi,[rbx]
 * 10 - TQ2-Win64-Shipping.exe+2393C0A - E8 36BDB703           - call TQ2-Win64-Shipping.exe+5F0F945
 * 11 - TQ2-Win64-Shipping.exe+2393C0F - F3 0F59 05 D9B34E04   - mulss xmm0,[TQ2-Win64-Shipping.exe+687EFF0]
 * 12 - TQ2-Win64-Shipping.exe+2393C17 - F3 0F59 C6            - mulss xmm0,xmm6
 * 13 - TQ2-Win64-Shipping.exe+2393C1B - E8 E3BCB703           - call TQ2-Win64-Shipping.exe+5F0F903
 * 14 - TQ2-Win64-Shipping.exe+2393C20 - F3 0F59 05 B800F903   - mulss xmm0,[TQ2-Win64-Shipping.exe+6323CE0]
 * 15 - TQ2-Win64-Shipping.exe+2393C28 - 48 8B CB              - mov rcx,rbx
 * 16 - TQ2-Win64-Shipping.exe+2393C2B - 0F28 C8               - movaps xmm1,xmm0
 * 17 - TQ2-Win64-Shipping.exe+2393C2E - 48 8B 87 D0050000     - mov rax,[rdi+000005D0]
 * 18 - TQ2-Win64-Shipping.exe+2393C35 - 48 8B 7C 24 40        - mov rdi,[rsp+40]
 * 19 - TQ2-Win64-Shipping.exe+2393C3A - 0F28 74 24 20         - movaps xmm6,[rsp+20]
 * 20 - TQ2-Win64-Shipping.exe+2393C3F - 48 83 C4 30           - add rsp,30
 * 21 - TQ2-Win64-Shipping.exe+2393C43 - 5B                    - pop rbx
 * 22 - TQ2-Win64-Shipping.exe+2393C44 - 48 FF E0              - jmp rax
 *
 * There isn't much to say here, but this is a HOR+ calculation for FOV. Meaning that this game when the horizontal
 * screen resolution increases the game will recalculate the FOV so it is not zoomed in for ultrawide users. Many
 * games provide ultrawide support but then dont do HOR+ scaling and leave it at VERT- so the FOV stays the same
 * and those users have to play with a zoomed in camera. Of course this is just one of many steps to do to achieve
 * HOR+ scaling, but I'm including it for your viewing.
 *
 * Also, the data that determines the viewport rectangle and the enable for it can be seen on lines 3 and 7. As stated
 * above we know that if this code gets executed xmm6 will contain the lesser value between either our screen or the
 * harded 21:9 value and that is fed as seen in line 3. And line 7 is basically the enable for whether or not the value
 * written by line 3 should be used or not. I assume somewhere in the code itself it checks this enable and if it's
 * true (1) then use the value written by line 3 else if its false (0) use the user's screen resolution ratio instead.
 *
 * Which brings me to my next point and block of code:
 * 1 - TQ2-Win64-Shipping.exe+2393C47 - F3 0F10 4C 24 60        - movss xmm1,[rsp+60]
 * 2 - TQ2-Win64-Shipping.exe+2393C4D - C7 80 B0020000 398EE33F - mov [rax+000002B0],3FE38E39
 * 3 - TQ2-Win64-Shipping.exe+2393C57 - 48 8B 89 E0090000       - mov rcx,[rcx+000009E0]
 * 4 - TQ2-Win64-Shipping.exe+2393C5E - 48 8B 01                - mov rax,[rcx]
 * 5 - TQ2-Win64-Shipping.exe+2393C61 - FF 90 D0050000          - call qword ptr [rax+000005D0]
 * 6 - TQ2-Win64-Shipping.exe+2393C67 - 48 8B 83 E0090000       - mov rax,[rbx+000009E0]
 * 7 - TQ2-Win64-Shipping.exe+2393C6E - 80 A0 B4020000 FE       - and byte ptr [rax+000002B4],-02
 *
 * This is what executes when you dont need to apply a pillarbox. Same thing here notice the same assignments on lines 2
 * and 7. But on line 7 we basically mask off the enable bit, do a bitwise and with 0xFE and then write it back. So the
 * viewport that was written on line 3 does not take effect. But if the enable bit is set on line 7 then the viewport is
 * used and that fancy hex value is the aspect ratio float for 16:9 (1.78).
 *
 * @return void
 */
void pillarBoxFix() {
    Utils::SignaturePatch sp = {
        .signature = "80 3D ?? ?? ?? ?? 00    74 78    F3 0F 10 44 24 60",
        .patch = "01",
        .patchOffset = 6
    };

    bool enable = yml.masterEnable && yml.fixes.pillarbox.enable;
    Utils::injectPatch(enable, module, sp);
}

/**
 * @brief Modifies the ingame FOV.
 *
 * @details
 * The game default FOV is 75 degrees. This feature if enabled allows you to change that value based on what
 * is provided in the YAML file and if this feature is enabled. There is no error or sanity checking, therefore
 * if a user provides a bad value in the YAML file, whether that be negative or a value that is too high or low
 * the camera will break.
 *
 * How was this found?
 * As part of the pillarbox fix the FOV exposed itself there given the HOR+ scaling that happens when ultrawide
 * ratios are detected. It was easy to see in the registers during a breakpoint that 75 was being used during the
 * trigonemetric tan calculations when loaded into xmm0 when the pillarbox fix was enabled, from the following
 * instruction:
 * TQ2-Win64-Shipping.exe+2393BCF - F3 0F10 44 24 60      - movss xmm0,[rsp+60]
 *
 * Or if the pillabox fix is not enabled then you would load 75.0f into xmm1 as follows:
 * TQ2-Win64-Shipping.exe+2393C47 - F3 0F10 4C 24 60      - movss xmm1,[rsp+60]
 *
 * Frankly I don't like the injection I would need to do here, either at the start of the functionI would need
 * to modify the value at rsp+60 or I would need to two hooks to modify xmm0 and xmm1 just after they are loaded.
 *
 * I needed something more simple, so I scanned for where the write to the memory location pointed to by rsp+60 was
 * happening and found out that it happens just before this function is called:
 * 1 - TQ2-Win64-Shipping.exe+22C8BAA - F3 0F11 44 24 20  - movss [rsp+20],xmm0
 * 2 - TQ2-Win64-Shipping.exe+22C8BB0 - E8 DBAF0C00       - call TQ2-Win64-Shipping.exe+2393B90
 *
 * This is much better place to inject right before xmm0, which holds the FOV value we need to change, gets written
 * to rsp+20 which is the same memory location as that pointed to by rsp+60 inside the function that is called on
 * line 2.
 *
 * So a hook is injected at line 1 and that replaces the value held by xmm0 with the value provided in the YAML
 * file.
 *
 * @note When increasing the FOV only the camera is affected, this feature has no effect on view frustum culling,
 * so now that the user can see more of the game world, they can also inadvertently see a lot more pop in and out
 * of various game assets towards the edges of the screen.
 *
 * @return void
 */
void fovFeature() {
    Utils::SignatureHook hook = {
        .signature = "F3 0F 11 44 24 20    E8 ?? ?? ?? ??    48 8B 5C 24 50    48 83 C4 40    5F    C3    48 89 5C 24 08",
    };

    bool enable = yml.masterEnable && yml.features.fov.enable;
    Utils::injectHook(enable, module, hook,
        [](SafetyHookContext& ctx) {
            ctx.xmm0.f32[0] = yml.features.fov.value;
        }
    );
}

/**
 * @brief Increases HUD size by 12.5%.
 *
 * @details
 * The increase is just enough to make the HUD bigger, but not enough to make elements go off screen.
 *
 * How was this found?
 * HUD is almost always the hardest part to modify.
 *
 * When it comes to HUD modifications the first thing I like to do is scan for 1.0f and start modifying any hits
 * to something like 0.5f. I do this because almost always if not everytime the HUD calculation in some part or
 * another will use 1.0f, as this is a common value for scaling and normalization.
 *
 * So I did that and eventually I found something that did let me change the HUD size and that 1.0f instance led
 * me to this small function:
 * 1  - TQ2-Win64-Shipping.exe+53BFC20 - 48 89 5C 24 18        - mov [rsp+18],rbx
 * 2  - TQ2-Win64-Shipping.exe+53BFC25 - 57                    - push rdi
 * 3  - TQ2-Win64-Shipping.exe+53BFC26 - 48 83 EC 20           - sub rsp,20
 * 4  - TQ2-Win64-Shipping.exe+53BFC2A - 80 B9 A8020000 00     - cmp byte ptr [rcx+000002A8],00
 * 5  - TQ2-Win64-Shipping.exe+53BFC31 - 48 8B DA              - mov rbx,rdx
 * 6  - TQ2-Win64-Shipping.exe+53BFC34 - 48 8B F9              - mov rdi,rcx
 * 7  - TQ2-Win64-Shipping.exe+53BFC37 - 74 21                 - je TQ2-Win64-Shipping.exe+53BFC5A
 * 8  - TQ2-Win64-Shipping.exe+53BFC39 - 3B 99 A0020000        - cmp ebx,[rcx+000002A0]
 * 9  - TQ2-Win64-Shipping.exe+53BFC3F - 75 19                 - jne TQ2-Win64-Shipping.exe+53BFC5A
 * 10 - TQ2-Win64-Shipping.exe+53BFC41 - 48 8B C2              - mov rax,rdx
 * 11 - TQ2-Win64-Shipping.exe+53BFC44 - 48 C1 E8 20           - shr rax,20
 * 12 - TQ2-Win64-Shipping.exe+53BFC48 - 3B 81 A4020000        - cmp eax,[rcx+000002A4]
 * 13 - TQ2-Win64-Shipping.exe+53BFC4E - 75 0A                 - jne TQ2-Win64-Shipping.exe+53BFC5A
 * 14 - TQ2-Win64-Shipping.exe+53BFC50 - F3 0F10 81 AC020000   - movss xmm0,[rcx+000002AC]
 * 15 - TQ2-Win64-Shipping.exe+53BFC58 - EB 31                 - jmp TQ2-Win64-Shipping.exe+53BFC8B
 * 16 - TQ2-Win64-Shipping.exe+53BFC5A - 4C 8D 44 24 30        - lea r8,[rsp+30]
 * 17 - TQ2-Win64-Shipping.exe+53BFC5F - E8 3CD6FEFF           - call TQ2-Win64-Shipping.exe+53AD2A0
 * 18 - TQ2-Win64-Shipping.exe+53BFC64 - 80 7C 24 30 00        - cmp byte ptr [rsp+30],00
 * 19 - TQ2-Win64-Shipping.exe+53BFC69 - 75 20                 - jne TQ2-Win64-Shipping.exe+53BFC8B
 * 20 - TQ2-Win64-Shipping.exe+53BFC6B - 48 8D 87 A0020000     - lea rax,[rdi+000002A0]
 * 21 - TQ2-Win64-Shipping.exe+53BFC72 - F3 0F11 87 AC020000   - movss [rdi+000002AC],xmm0
 * 22 - TQ2-Win64-Shipping.exe+53BFC7A - 48 8D 4C 24 38        - lea rcx,[rsp+38]
 * 23 - TQ2-Win64-Shipping.exe+53BFC7F - 48 3B C8              - cmp rcx,rax
 * 24 - TQ2-Win64-Shipping.exe+53BFC82 - 74 07                 - je TQ2-Win64-Shipping.exe+53BFC8B
 * 25 - TQ2-Win64-Shipping.exe+53BFC84 - 48 89 18              - mov [rax],rbx
 * 26 - TQ2-Win64-Shipping.exe+53BFC87 - C6 40 08 01           - mov byte ptr [rax+08],01
 * 27 - TQ2-Win64-Shipping.exe+53BFC8B - F3 0F59 87 C0010000   - mulss xmm0,[rdi+000001C0]
 * 28 - TQ2-Win64-Shipping.exe+53BFC93 - 48 8B 5C 24 40        - mov rbx,[rsp+40]
 * 29 - TQ2-Win64-Shipping.exe+53BFC98 - F3 0F5F 05 9C7DCD00   - maxss xmm0,[TQ2-Win64-Shipping.D3D12SDKVersion+4E18]
 * 30 - TQ2-Win64-Shipping.exe+53BFCA0 - 48 83 C4 20           - add rsp,20
 * 31 - TQ2-Win64-Shipping.exe+53BFCA4 - 5F                    - pop rdi
 * 32 - TQ2-Win64-Shipping.exe+53BFCA5 - C3                    - ret
 *
 * I will ignore most of the code to focus on the bits that make this feature work. Line 14 is the important line
 * where the magic 1.0f is read that is the global scaler for the HUD. In terms of percetages think of 1.0f as
 * 100% so HUD is the default size as the developers intended it to be. That register will always contain that
 * value in this block and will be returned when the function returns. Aother point of interest is line 27, where
 * the 1.0f is multiplied by another scaler, that location also has 1.0f, so nothing is scaled, and on line 29 the
 * game performs a quick check against 0.01f (value at TQ2-Win64-Shipping.D3D12SDKVersion+4E18). And the game takes
 * the greater value of the two. Its a strange check to make, since if the value is so small that it makes the HUD
 * invisible, and just making NOP'ing this instruction and forcing a 0 into xmm0 has no bad consequences, so its
 * wierd why this check is here.
 *
 * But anyway back to the main point, after that multiply operation, we can inject a new value into the xmm0
 * register, and with experimentation I found that injecting 1.125f (112.5% if you wanna think in percentages). So
 * that will make the HUD 12.5% bigger, I hoped I could push it further, but that value is just enough where certain
 * UI elements don't go offscreen. This scales everything HUD wise, and in reverse if you go smaller than everything
 * becomes smaller, that option is there too.
 *
 * And just for fun, when you return from this function, the game also applies a division operation to the value in
 * xmm0. Xmm0 is divided once again by 1.0f, so nothing changes. Yet another scaler operation is applied that doesn't
 * do anything.
 *
 * @note As of v1.2.1 it has been discovered that the HUD scaler is not a constant 1.0f value, but rather it's a ratio
 * between the current game selected resolution and the physical desktop resolution. For example if the desktop is
 * 3840x2160 and the ingame resolution is 1920x1080, the HUD scaler will be 0.5f (1080/2160). In v1.2.1 we take that
 * into consideration and instead multiply what is already in the xmm0 regiter by 12.5% rather than hardcode 1.125f
 * like it was done previously.
 *
 * @return void
 */
void hudFeature() {
    Utils::SignatureHook hook = {
        .signature = "48 8B 5C 24 40    F3 0F 5F 05 ?? ?? ?? ??",
    };

    bool enable = yml.masterEnable && yml.features.hud.enable;
    Utils::injectHook(enable, module, hook,
        [](SafetyHookContext& ctx) {
            ctx.xmm0.f32[0] += (ctx.xmm0.f32[0] * 0.125f);
        }
    );
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
    fovFeature();
    hudFeature();
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
