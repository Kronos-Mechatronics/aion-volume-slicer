# Aion-5X Slicer Plugin

This repository contains the Prusaslicer plugin for Aion-5X. The plugin allows users to slice volumetric objects and print them using Aion-5X on compatible 3D printers. The plugin needs Aion-5X to function. If you are interested in 5-axis 3D printing and want to learn more about the capabilities of Aion-5X, please visit the [Aion-5X website](https://kronos-mct.com/).

Aion Prusaslicer plugin


## Setup build environment
The required software packages for Aion development are listed below. For Visual Studio and Rhino follow the official guide here: https://developer.rhino3d.com/guides/cpp/installing-tools-windows/ .  
Visual Studio 2022 is assumed in the following steps.
Make sure to install ATL, MFC and SDK for VS 2019 compiler as described in the link above.

    Visual Studio 2022 (community edition is free)
    Rhino 8
    Rhino 8 SDK
    Aion-5X SDK
    CMake
    Git

For downloading the code, clone it with git to the disk (<path/to/repository> need to be replaced with a propper path on the local disk):

    cd <path/to/repository>
    git clone <repository_url>

### External dependencies
We need some external libraries. Those are pre-build into a separate directory like this:

    cd <path/to/repository>
    cd lib
    mkdir build
    cd build
    cmake .. -G "Visual Studio 17 2022" -A x64 -T v142 -DDESTDIR="c:/software/Aion-slicer-libs"
    cmake --build .

This will download (git clone) all required libraries into the `build` folder, build them and finally install binaries, .h files etc into `c:/software/Aion-slicer-libs`.
External libraries should usually be statically linked into the plugin.
As the Prusaslicer is a very large project, this will take a while. The build process will produce a lot of output, so be patient.

Note: apparently, if both Visual Studio 2019 and Visual Studio 2022 are installed, using 2022 with 2019 toolset fails for unclear reasons, throwing this error:

    CMake Error at CMakeLists.txt:4 (project):
      No CMAKE_CXX_COMPILER could be found.


In that case, use Visual Studio 2019 as build tool like this:

    cmake .. -G "Visual Studio 16 2019" -DDESTDIR="c:/software/Aion-slicer-libs"

**Important technical note**: all external libs are build in Release mode.
Rhino plugins are always build in release mode, but with /Zi and /DEBUG flags turned on.
The reason seems to be, that Rhino itself is only shipped as release build.
Documentation is here: https://developer.rhino3d.com/guides/cpp/your-first-plugin-windows/#testing
So if we build external libs in debug mode, VS tries to link the static plugin library against debug libraries, resulting in an `_ITERATOR_DEBUG_LEVEL': value '2' doesn't match value '0'` error.

### Building the plugin dll

Building the plugin requires the Aion-5X SDK to be installed. The SDK is not included in this repository, but can be downloaded with as a customer from the [Aion-5X download page](https://documentation.kronos-mct.com/aion/latest/download/). The SDK should be installed in a directory alogside this repository. If the SDK is installed in a different location, set the location with `-DAION_SDK_DIR="<path/to/Aion-5X-SDK>"` when running cmake.

To build from terminal (e.g. git-bash / PowerShell) do something like this:

    cd <path/to/repository>
    mkdir build
    cd build
    cmake .. -G "Visual Studio 17 2022" -A x64 -T v142 -DCMAKE_PREFIX_PATH="c:/software/Aion-slicer-libs/"
    cmake --build .

this will produce the binary `Aion-Slicer.rhp` in `<path/to/repository>/build/Debug/`  
To build a release version of the binary, just change `cmake --build .` to `cmake --build . --config Release` in the last command. 

The generated .sln file can be opened in Visual Studio from the build directory to build the plugin from there.