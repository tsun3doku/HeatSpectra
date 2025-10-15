# HeatSpectra

Realtime thermal analysis visualization using tetrahedral mesh elements and compute shaders

![Heat Transfer Capture](x64/Release/capture.png)

## Requirements
- Windows 10
- Vulkan SDK 1.3
- Visual Studio 

## Quick Start
1. Download the latest demo release from the [Releases](https://github.com/tsun3doku/HeatSpectra/releases) page
2. Extract the zip file
3. Run HeatSpectra.exe

### Hardware Requirements
- GPU with Vulkan 1.3 or higher support ([Check GPU compatibility](https://vulkan.gpuinfo.org/))

## Setup
1. Clone the repository
2. Build shaders using `compile.bat` in `shaders` folder
3. Open solution in Visual Studio
4. Build and run in Release mode

## Controls
-  Arrow keys for heat source control

## TODO
- Remeshing is currently cosmetic only
