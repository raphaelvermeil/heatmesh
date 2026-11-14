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

# --- Eigen: linear algebra (sparse matrices + solvers), for Step 13 -----------
# Eigen is header-only. Its own CMakeLists.txt sets up tests, docs, etc. that we
# don't need, so SOURCE_SUBDIR points at a folder that doesn't exist: CMake then
# only downloads the sources, and we create a tiny target for them below.
FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-configure)

# --- portable-file-dialogs: native "Open file" dialog (Windows/macOS/Linux) ----
# Header-only too; same trick as Eigen: download only, don't run its CMake.
FetchContent_Declare(pfd
    GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
    GIT_TAG        0.1.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-configure)

FetchContent_MakeAvailable(glfw glm imgui tinyobjloader eigen pfd)

add_library(pfd INTERFACE)
target_include_directories(pfd INTERFACE ${pfd_SOURCE_DIR})
# On Windows the dialog uses COM system libraries. MSVC finds them by itself
# (the header asks for them with #pragma comment), MinGW needs them spelled out.
if(WIN32)
    target_link_libraries(pfd INTERFACE uuid ole32 shell32)
endif()

# Eigen: an INTERFACE library has no compiled code; linking to it just adds
# its include folder to whoever links it.
add_library(eigen INTERFACE)
target_include_directories(eigen INTERFACE ${eigen_SOURCE_DIR})

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

add_executable(HeatMesh src/main.cpp src/shader.cpp src/camera.cpp src/mesh.cpp src/picking.cpp
                        src/diffusion.cpp src/heat_solver.cpp)
target_link_libraries(HeatMesh PRIVATE glad glfw glm::glm imgui tinyobjloader eigen pfd)

# Eigen is extremely slow without optimizations (a Debug build can be 50x slower).
# All Eigen code lives in heat_solver.cpp, so we optimize just that one file,
# even in Debug. The rest of the program stays debuggable line by line.
if(NOT MSVC)
    set_source_files_properties(src/heat_solver.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()

# Lets the program find assets/ and shaders/ no matter where CLion runs it from
target_compile_definitions(HeatMesh PRIVATE PROJECT_DIR="${CMAKE_SOURCE_DIR}")
