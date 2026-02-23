# Titan Quest II Fix
![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/PolarWizard/TitanQuest2Fix/total)

Ultrawide fixes and other enhancements.

***This project is designed exclusively for Windows due to its reliance on Windows-specific APIs. The build process requires the use of PowerShell.***

## Build and Install
### Using CMake
1. Build and install:
```ps1
git clone https://github.com/PolarWizard/TitanQuest2Fix.git
cd TitanQuest2Fix
# If install is not needed you may omit -DCMAKE_INSTALL_PREFIX and cmake install step.
cmake -B build -DCMAKE_INSTALL_PREFIX=<FULL-PATH-TO-GAME-FOLDER> ..
cmake --build build --config Release
cmake --install build
```
2. Download [dsound.dll](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) Win64 version
3. Extract to game folder: `Titan Quest II/TQ2/Binaries/Win64`

### Using VSCode Task
1. `Ctrl+Shift+P`
2. `>Tasks: Run Task`
3. Run task group `configure, build, and install` or individually `configure`, `build`, and `install`

### Using Release
Download and follow instructions in [latest release](https://github.com/PolarWizard/TitanQuest2Fix/releases)

## Configuration
- Adjust settings in `Titan Quest II/TQ2/Binaries/Win64/scripts/TitanQuest2Fix.yml`

## Screenshots
| ![Demo1](images/TitanQuest2Fix_1.gif) |
| --- |
| <p align='center'> Fix disabled → Fix enabled </p> |

## License
Distributed under the MIT License. See [LICENSES](LICENSES) for more information.

## External Tools
- [safetyhook](https://github.com/cursey/safetyhook)
- [spdlog](https://github.com/gabime/spdlog)
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- [yaml-cpp](https://github.com/jbeder/yaml-cpp)
- [zydis](https://github.com/zyantific/zydis)
