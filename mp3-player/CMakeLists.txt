cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0072 NEW) # 抑制 OpenGL 警告
project(mp3-player)

# 设置 C++ 标准为 C++17，以支持 filesystem 库
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找必要的库
find_package(SDL2 REQUIRED)

find_package(OpenGL REQUIRED)

# 添加 ImGui 库的源文件
add_library(imgui
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_sdl2.cpp
    vendor/imgui/backends/imgui_impl_opengl3.cpp
)

# 为 ImGui 库设置头文件目录
target_include_directories(imgui PUBLIC
    vendor/imgui
    vendor/imgui/backends
    /usr/include/SDL2 # 手动添加 SDL2 头文件路径到 imgui 库
)

# 为 imgui 库链接它所依赖的库
# 使用 ${SDL2_LIBRARIES} 变量链接 SDL2 库
target_link_libraries(imgui PUBLIC
    ${SDL2_LIBRARIES}
    OpenGL::GL
)

# 添加 miniaudio 实现的编译定义
add_compile_definitions(MINIAUDIO_IMPLEMENTATION)

# 创建可执行文件
add_executable(mp3-player main.cpp)

# 链接所需的库到主程序
# 将 ${CMAKE_THREAD_LIBS_INIT} 替换为 pthread
target_link_libraries(mp3-player PRIVATE imgui ${CMAKE_DL_LIBS} pthread)

# 为主程序设置头文件目录
# miniaudio 目录仍然是主程序需要的
target_include_directories(mp3-player PRIVATE
    vendor/miniaudio
    /usr/include/SDL2
)