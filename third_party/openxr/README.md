# OpenXR headers

The OpenXR API headers from the Khronos Group's [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK), tag
`release-1.1.65` (commit 24a1f2b92d212501a2b1b8d30ffd53abf55fcfa8), `include/openxr/*.h`, unchanged. Apache 2.0
(`LICENSE`).

Only the headers are here, for the VR mode (`make VR=1`, docs/VR.md) and its tests. The loader they link against is the
system's (MSYS2 `mingw-w64-ucrt-x86_64-openxr-sdk`, Debian / Ubuntu `libopenxr-dev`).
