# typofs —— CMake 构建（仅 Windows / MSVC，只用 Windows SDK）
#
# 无第三方依赖：HTTP 用 WinHTTP、签名用 CNG/BCrypt，
# 只需要 Windows SDK（装了 MSVC 就有），不需要 vcpkg / curl / openssl。
#
# 构建：
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
# 产物：build\Release\typofs.exe

cmake_minimum_required(VERSION 3.16)
project(typofs C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS)
endif()

add_executable(typofs main.c)

target_link_libraries(typofs PRIVATE winhttp bcrypt)
