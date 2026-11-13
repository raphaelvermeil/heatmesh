cmake_minimum_required(VERSION 3.20)
project(HeatMesh CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- GLFW: window + keyboard/mouse input -------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

# --- GLM: vector/matrix math (vec3, mat4, perspective, lookAt) ---------------
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

# --- Dear ImGui: sliders/buttons/panels (no CMake of its own, see below) -----
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE)

# --- tinyobjloader: reads .obj mesh files -------------------------------------
FetchContent_Declare(tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG        v2.0.0rc13
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(glfw glm imgui tinyobjloader)

# glad: generated once (OpenGL 4.1 core + KHR_debug) and checked in under external/
add_library(glad STATIC external/glad/src/gl.c)
target_include_directories(glad PUBLIC external/glad/include)

# ImGui: compile its sources plus the GLFW + OpenGL3 backends
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

add_executable(HeatMesh src/main.cpp src/shader.cpp src/camera.cpp src/mesh.cpp)
target_link_libraries(HeatMesh PRIVATE glad glfw glm::glm imgui tinyobjloader)

# Lets the program find assets/ and shaders/ no matter where CLion runs it from
target_compile_definitions(HeatMesh PRIVATE PROJECT_DIR="${CMAKE_SOURCE_DIR}")
