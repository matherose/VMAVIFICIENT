# macOS x86_64 toolchain (Apple Clang from Xcode).
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_OSX_ARCHITECTURES x86_64)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

find_program(_clang clang REQUIRED)
set(CMAKE_C_COMPILER "${_clang}")
