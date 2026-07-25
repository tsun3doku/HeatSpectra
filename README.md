# ParaMetal

[![Website](https://img.shields.io/badge/web-parametal.com-blue)](https://parametal.com/)
[![GitHub release](https://img.shields.io/github/v/release/tsun3doku/ParaMetal)](https://github.com/tsun3doku/ParaMetal/releases)

![Heat Transfer](ParaMetal/docs/heat.gif)

This project allows the visualization of heat transfer between two or more 3D closed surface geometry. The simulation is deterministic, transient and operates in realtime using stable pre-processing methods. 

![Voronoi](ParaMetal/docs/voronoi.png)

The major pre-processing methods include an intrinsic remeshing operation that preserves the shape of the geometry and a meshless restricted voronoi diagram step that discretizes the volume of the surface boundary.

![Contact](ParaMetal/docs/contact.png)

This project is a work in progress. Functionality, performance and physical accuracy will be continuously updated.

## Quick Start
1. Download the latest release from the [Releases](https://github.com/tsun3doku/ParaMetal/releases) page
2. Extract the zip file
3. Run parametal.exe

### System Requirements
- 64-bit Windows 10 or Windows 11
- NVIDIA GPU with CUDA compute capability 7.5 or newer
- Vulkan 1.3 or higher support ([Check GPU compatibility](https://vulkan.gpuinfo.org/))

The downloadable release includes the required CUDA and AMGX runtime libraries. Building ParaMetal from source additionally requires the development tools below.

### Source Build Prerequisites
- [CMake](https://cmake.org/download/)
- [Microsoft Visual C++ (MSVC) Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) with Desktop development with C++
- [Qt 6](https://www.qt.io/download) for MSVC (Core, Gui, Widgets components)
- [Vulkan SDK](https://vulkan.lunarg.com/) 1.3 or higher
- [CUDA Toolkit](https://developer.nvidia.com/cuda-downloads) 12.0 or higher
- [Python 3](https://www.python.org/downloads/) (Development headers required)

Eigen, the AMGX headers, Pybind11 and the remaining dependencies are included as Git submodules. A matching prebuilt AMGX shared library is included under `ParaMetal/libs/amgx_prebuilt/windows-x64`; you do not need to compile AMGX yourself.

### Build From Source

1. Clone the repository with submodules:
   ```powershell
   git clone --recursive https://github.com/tsun3doku/ParaMetal.git
   cd ParaMetal
   ```

2. Configure and build ParaMetal, replacing the Qt path with your MSVC Qt kit:
   ```powershell
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/path/to/Qt/6.x.x/msvc2022_64"
   cmake --build build --config Release --parallel
   ```

3. Run `parametal.exe` from `build/Release` when using a multi-configuration generator, or from `build` when using a single-configuration generator.
