# Windows x64 cross-build. The compiler comes from the windows-build module (llvm-mingw, clang + lld, ucrt) when
# x86_64-w64-mingw32-clang is on PATH; otherwise it falls back to the GCC-based mingw-w64 (Docker image, distro package).
# SDL2_MINGW_ROOT points at the SDL2 mingw development files (x86_64-w64-mingw32 folder).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
find_program(RENSGA_MINGW_CLANG x86_64-w64-mingw32-clang)
if(RENSGA_MINGW_CLANG)
  set(CMAKE_C_COMPILER x86_64-w64-mingw32-clang)
  set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-clang++)
  set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
  # Static runtime: the exe needs only SDL2.dll next to it, no libc++ / libunwind DLLs.
  set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
  set(CMAKE_FIND_ROOT_PATH $ENV{SDL2_MINGW_ROOT})
else()
  set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
  set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
  set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
  set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 $ENV{SDL2_MINGW_ROOT})
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
