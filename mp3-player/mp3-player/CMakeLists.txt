# CMakeLists.txt (最终完整版)

cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0072 NEW) 
project(mp3-player)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ======== 查找所有需要的库 ========
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(OpenSSL REQUIRED) 
find_package(CURL REQUIRED)
find_package(Threads REQUIRED) 
find_package(ixwebsocket REQUIRED) # <-- 补上对 ixwebsocket 的查找

# ... add_library(imgui) 和相关部分保持不变 ...
add_library(imgui
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_sdl2.cpp
    vendor/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    vendor/imgui
    vendor/imgui/backends
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(imgui PUBLIC
    ${SDL2_LIBRARIES}
    OpenGL::GL
)
# ................................................................


add_compile_definitions(MINIAUDIO_IMPLEMENTATION)
add_executable(mp3-player main.cpp)

# ======== 链接所有需要的库 ========
target_link_libraries(mp3-player PRIVATE
    imgui
    ${CMAKE_DL_LIBS}
    Threads::Threads     
    OpenSSL::SSL         
    OpenSSL::Crypto      
    CURL::libcurl
    ixwebsocket::ixwebsocket # <-- 补上链接 ixwebsocket 库
)

target_include_directories(mp3-player PRIVATE
    vendor/miniaudio
)