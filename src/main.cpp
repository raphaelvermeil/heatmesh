// HeatMesh: real-time heat diffusion on a 3D mesh.
// main.cpp owns the window, the main loop and (for now) the scene setup.

#include <glad/gl.h>      // must come before GLFW
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <portable-file-dialogs.h>  // native "Open file" dialog

#include <glm/glm.hpp>                    // vec3, mat4, ...
#include <glm/gtc/matrix_transform.hpp>   // rotate, lookAt, perspective
#include <glm/gtc/type_ptr.hpp>           // value_ptr (matrix -> float* for OpenGL)

#include <algorithm>  // std::min, std::max, std::fill
#include <chrono>     // timing the diffusion
#include <cmath>      // std::sin
#include <cstdint>    // SIZE_MAX
#include <cstdio>
#include <cstdlib>    // std::rand
#include <filesystem> // std::filesystem::path (file names, folder separators)
#include <optional>   // std::optional
#include <string>
#include <vector>

#include "camera.h"
#include "colormap.h"
#include "diffusion.h"
#include "heat_solver.h"
#include "mesh.h"
#include "picking.h"
#include "shader.h"  // our own header: quotes = "look in my project first"

// Prints OpenGL errors as they happen (Windows/Linux drivers; macOS doesn't support it).
static void GLAPIENTRY onGlDebug(GLenum, GLenum type, GLuint, GLenum severity,
                                 GLsizei, const GLchar* message, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;  // skip info spam
    std::fprintf(stderr, "[GL %s] %s\n",
                 type == GL_DEBUG_TYPE_ERROR ? "ERROR" : "warn", message);
}

// --- Step 13: which diffusion method runs ---
// 'enum class' = a type with a fixed list of named values. Unlike a plain int
// (0 = graph, 1 = cotangent), you can't mix it up with a number by accident,
// and the names must be written with their prefix: DiffusionMethod::GraphAverage.
enum class DiffusionMethod { GraphAverage, CotangentImplicit };

// The implicit solver always advances by this much simulated time per step.
// Fixed, because the matrix (M + k*h*L) is factorized for one specific h.
constexpr float kImplicitStep = 1.0f / 60.0f;  // seconds

// Turns {7, 9, 13} into the text "7 9 13" for display.
static std::string neighborListText(const std::vector<unsigned int>& list) {
    std::string text;
    for (unsigned int v : list) {
        text += std::to_string(v);  // number -> text
        text += ' ';
    }
    return text;
}

// --- Temperature test patterns (Step 8) ---
// 'const Mesh&' = read the mesh without copying it; 'std::vector<float>& out'
// = write into the caller's vector. Same in/out pattern as loadObj().

// Bottom of the bunny cold (0), top hot (1). After normalizeToUnitBox the
// y coordinates are within -0.5..0.5, so y + 0.5 is within 0..1.
static void fillHeightGradient(const Mesh& mesh, std::vector<float>& out) {
    for (size_t i = 0; i < mesh.vertexCount(); ++i)
        out[i] = mesh.positions[i].y + 0.5f;
}

// Horizontal bands of heat that travel upwards over time.
// sin() swings between -1 and 1, so 0.5 + 0.5*sin(...) swings between 0 and 1.
static void fillWave(const Mesh& mesh, float time, std::vector<float>& out) {
    for (size_t i = 0; i < mesh.vertexCount(); ++i)
        out[i] = 0.5f + 0.5f * std::sin(12.0f * mesh.positions[i].y - 4.0f * time);
}

// --- Heat brush (Steps 10 + 12) ---
// Heats every vertex within 'radius' of 'center': 'amount' at the center, fading
// linearly to 0 at the edge. std::max keeps the hotter value, so brushing
// over an already-hot area never cools it down.
// The nearest vertex always gets 'amount', so even radius 0 heats something.
static void applyHeatBrush(const Mesh& mesh, glm::vec3 center, unsigned int nearestVertex,
                           float radius, float amount, std::vector<float>& temperature) {
    temperature[nearestVertex] = std::max(temperature[nearestVertex], amount);
    if (radius <= 0.0f) return;

    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        float d = glm::distance(mesh.positions[i], center);
        if (d < radius) {
            float heat = amount * (1.0f - d / radius);  // d = 0 -> amount, d = radius -> 0
            temperature[i] = std::max(temperature[i], heat);
        }
    }
}

// Draws a horizontal colour bar for the colormap inside the current ImGui window,
// with "cold" / "hot" labels under it.
static void drawColormapLegend(const Colormap& cm) {
    const float  width  = ImGui::GetContentRegionAvail().x;  // full width of the panel
    const float  height = 16.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();       // top-left corner, in pixels
    ImDrawList*  draw   = ImGui::GetWindowDrawList();

    // A smooth bar = many thin rectangles, each with a left and a right colour.
    const int segments = 64;
    for (int s = 0; s < segments; ++s) {
        float t0 = static_cast<float>(s) / segments;
        float t1 = static_cast<float>(s + 1) / segments;
        glm::vec3 c0 = sampleColormap(cm, t0);
        glm::vec3 c1 = sampleColormap(cm, t1);
        ImU32 left  = ImGui::ColorConvertFloat4ToU32(ImVec4(c0.r, c0.g, c0.b, 1.0f));
        ImU32 right = ImGui::ColorConvertFloat4ToU32(ImVec4(c1.r, c1.g, c1.b, 1.0f));
        ImVec2 a(origin.x + width * t0, origin.y);            // top-left of this slice
        ImVec2 b(origin.x + width * t1, origin.y + height);   // bottom-right
        // Corner colours in order: top-left, top-right, bottom-right, bottom-left.
        draw->AddRectFilledMultiColor(a, b, left, right, right, left);
    }
    // We drew "by hand", so tell ImGui's layout that this space is now used.
    ImGui::Dummy(ImVec2(width, height));

    ImGui::TextUnformatted("0 cold");
    ImGui::SameLine(width - ImGui::CalcTextSize("hot 1").x);  // right-align the label
    ImGui::TextUnformatted("hot 1");
}

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    // OpenGL 4.1 core: the highest version macOS supports, so this also runs on your Mac.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);  // required on macOS
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "HeatMesh", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Window creation failed (does your GPU support OpenGL 4.1?)\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // vsync

    // glad looks up the addresses of all OpenGL functions from the driver.
    int version = gladLoadGL(glfwGetProcAddress);
    if (!version) {
        std::fprintf(stderr, "gladLoadGL failed\n");
        return 1;
    }
    std::printf("OpenGL %d.%d | %s\n", GLAD_VERSION_MAJOR(version),
                GLAD_VERSION_MINOR(version), glGetString(GL_RENDERER));

    if (GLAD_GL_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);  // error reported on the line that caused it
        glDebugMessageCallback(onGlDebug, nullptr);
    }

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");

    float clearColor[3] = {0.12f, 0.12f, 0.15f};

    // PROJECT_DIR comes from CMakeLists.txt (target_compile_definitions), so the
    // asset and shader files are found no matter which folder the .exe runs from.
    GLuint program = loadShaderProgram(PROJECT_DIR "/shaders/mesh.vert",
                                       PROJECT_DIR "/shaders/mesh.frag");
    if (!program) return 1;

    // --- GPU objects: created ONCE, filled by loadMeshFile() (possibly many times) ---
    //
    // Same VAO/VBO/EBO idea as the cube. The buffers start empty: their contents
    // are uploaded by loadMeshFile() below, and replaced whenever a new .obj is
    // loaded. The VAO's attribute setup only refers to the buffer *objects*, so it
    // stays valid when their contents (and even their size) change.
    //
    // Positions, normals and temperatures live in *separate* VBOs (Step 6), so the
    // temperature (Step 8) can be updated every frame without touching the others.
    GLuint vao = 0, positionVbo = 0, normalVbo = 0, temperatureVbo = 0, ebo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &positionVbo);
    glGenBuffers(1, &normalVbo);
    glGenBuffers(1, &temperatureVbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    // glVertexAttribPointer records *whichever buffer is bound right now*, so
    // bind first, then describe.
    // Attribute 0 (aPos): 3 floats, stride sizeof(glm::vec3) = 12 bytes.
    glBindBuffer(GL_ARRAY_BUFFER, positionVbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);
    // Attribute 1 (aNormal): same layout, from the second buffer.
    glBindBuffer(GL_ARRAY_BUFFER, normalVbo);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(1);
    // Attribute 2 (aTemperature): ONE float per vertex. Stride = 4 bytes.
    glBindBuffer(GL_ARRAY_BUFFER, temperatureVbo);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);
    // The index buffer binding is stored inside the VAO.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBindVertexArray(0);  // unbind so later code can't accidentally modify it

    // --- Neighbour highlight: a second VAO that draws a few vertices as dots ---
    // It reuses positionVbo (no need to copy the positions again) but has its own
    // small index buffer: [selected, neighbour1, neighbour2, ...].
    GLuint highlightVao = 0, highlightEbo = 0;
    glGenVertexArrays(1, &highlightVao);
    glGenBuffers(1, &highlightEbo);
    glBindVertexArray(highlightVao);
    glBindBuffer(GL_ARRAY_BUFFER, positionVbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, highlightEbo);
    glBindVertexArray(0);

    // --- Everything that depends on the current mesh ---
    // Declared here (empty), filled by loadMeshFile().
    Mesh mesh;
    std::string meshName;           // file name shown in the panel, e.g. "bunny.obj"
    std::string meshError;          // last load error, shown in red; empty = none
    GLsizei indexCount = 0;         // number of indices in 'ebo'

    // temperature[i] belongs to vertex i, exactly like positions[i] and normals[i].
    // 0 = cold, 1 = hot (the colour mapping comes in the shader).
    std::vector<float> temperature;

    // Neighbour-count statistics, shown in the UI.
    size_t minNeighbors = 0, maxNeighbors = 0;
    float  avgNeighbors = 0.0f;

    int selectedVertex = 0;           // int, because ImGui::InputInt works with int
    GLsizei highlightCount = 0;       // how many indices are in highlightEbo
    std::optional<Hit> lastHit;       // empty until the first successful pick

    std::vector<float> scratch;       // work buffer for diffuse(), reused every frame
    ImplicitHeatSolver heatSolver;    // Step 13; built for each mesh in loadMeshFile()
    float implicitAccumulator = 0.0f; // real time not yet simulated, in seconds

    // Copies the CPU temperatures into the existing GPU buffer.
    // glBufferSubData overwrites bytes of a buffer that already exists, instead of
    // re-creating it like glBufferData does. Note: GL_ARRAY_BUFFER is *not* part of
    // the VAO's state (unlike GL_ELEMENT_ARRAY_BUFFER), so no VAO bind is needed.
    auto uploadTemperatures = [&]() {
        glBindBuffer(GL_ARRAY_BUFFER, temperatureVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, temperature.size() * sizeof(float),
                        temperature.data());
    };

    // Refills highlightEbo for a given vertex. Called on load and whenever the
    // selection changes.
    auto uploadHighlight = [&](unsigned int v) {
        std::vector<unsigned int> ids;
        ids.push_back(v);                                   // ids[0]: the selected vertex
        ids.insert(ids.end(), mesh.neighbors[v].begin(),    // ids[1..]: its neighbours
                   mesh.neighbors[v].end());
        highlightCount = static_cast<GLsizei>(ids.size());

        // The element buffer binding belongs to the VAO, so bind the VAO first.
        glBindVertexArray(highlightVao);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, ids.size() * sizeof(unsigned int),
                     ids.data(), GL_DYNAMIC_DRAW);  // DYNAMIC: we'll rewrite it often
        glBindVertexArray(0);
    };

    // --- Loading a mesh: at startup, and again from "Open .obj..." / drag & drop ---
    // Returns false (and keeps the current mesh) if the file can't be used.
    auto loadMeshFile = [&](const std::string& path) -> bool {
        // Load into a *temporary* mesh first. If anything fails halfway, the mesh
        // being displayed is untouched and the app keeps working.
        Mesh loaded;
        std::string error;
        if (!loadObj(path, loaded, error)) {
            meshError = error;
            std::fprintf(stderr, "Failed to load %s: %s\n", path.c_str(), error.c_str());
            return false;
        }
        normalizeToUnitBox(loaded);
        computeVertexNormals(loaded);  // after normalizing, so normals match the final shape
        buildAdjacency(loaded);

        // Success: replace the current mesh. std::move hands over the vectors'
        // memory instead of copying millions of numbers (explained in the walkthrough).
        mesh = std::move(loaded);
        meshError.clear();
        // Just the file name: "C:/models/teapot.obj" -> "teapot.obj".
        meshName = std::filesystem::u8path(path).filename().u8string();

        // Neighbour statistics. Most vertices of a typical triangle mesh have
        // ~6 neighbours; vertices on a hole's border have fewer.
        minNeighbors = SIZE_MAX;
        maxNeighbors = 0;
        size_t totalNeighbors = 0;
        for (const auto& list : mesh.neighbors) {
            minNeighbors    = std::min(minNeighbors, list.size());
            maxNeighbors    = std::max(maxNeighbors, list.size());
            totalNeighbors += list.size();
        }
        avgNeighbors = static_cast<float>(totalNeighbors) / mesh.vertexCount();
        std::printf("Adjacency: %zu..%zu neighbours per vertex, average %.2f\n",
                    minNeighbors, maxNeighbors, avgNeighbors);

        // Upload the new geometry. glBufferData (not SubData): the sizes differ
        // from the previous mesh, so each buffer is re-allocated at the new size.
        glBindBuffer(GL_ARRAY_BUFFER, positionVbo);
        glBufferData(GL_ARRAY_BUFFER, mesh.positions.size() * sizeof(glm::vec3),
                     mesh.positions.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, normalVbo);
        glBufferData(GL_ARRAY_BUFFER, mesh.normals.size() * sizeof(glm::vec3),
                     mesh.normals.data(), GL_STATIC_DRAW);

        // Everything starts cold. GL_DYNAMIC_DRAW: rewritten often (every frame).
        temperature.assign(mesh.vertexCount(), 0.0f);
        glBindBuffer(GL_ARRAY_BUFFER, temperatureVbo);
        glBufferData(GL_ARRAY_BUFFER, temperature.size() * sizeof(float),
                     temperature.data(), GL_DYNAMIC_DRAW);

        glBindVertexArray(vao);  // the EBO binding lives in the VAO: bind it first
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int),
                     mesh.indices.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);
        indexCount = static_cast<GLsizei>(mesh.indices.size());

        // Reset everything that pointed into the old mesh: vertex numbers from the
        // old mesh mean nothing (or are out of range) in the new one.
        selectedVertex = 0;
        uploadHighlight(0);
        lastHit.reset();               // empty the optional
        scratch.clear();
        heatSolver.build(mesh);        // new L and M; the factorization is redone lazily
        implicitAccumulator = 0.0f;
        return true;
    };

    if (!loadMeshFile(PROJECT_DIR "/assets/bunny.obj")) return 1;

    // A mesh file waiting to be loaded at the start of the next frame. Set by
    // drag & drop and by the "Open .obj..." button; empty = nothing to load.
    std::string pendingMeshPath;

    // --- Drag & drop: GLFW calls this function when files are dropped on the window ---
    // A C callback can't capture variables like a lambda can, so we park a pointer
    // to 'pendingMeshPath' in the window ("user pointer") and fetch it back inside.
    // The loop then loads the file: OpenGL work is best kept out of callbacks.
    glfwSetWindowUserPointer(window, &pendingMeshPath);
    // A lambda with an empty [] captures nothing, so it converts to a plain
    // function pointer, which is what GLFW expects.
    glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) {
        if (count < 1) return;
        auto* target = static_cast<std::string*>(glfwGetWindowUserPointer(w));
        *target = paths[0];  // GLFW gives UTF-8 paths; we only take the first file
    });

    // Uniforms are shader variables set from C++ that stay constant for a whole draw call
    // (unlike attributes, which differ per vertex). We look up their "location" once.
    const GLint uModelLoc      = glGetUniformLocation(program, "uModel");
    const GLint uViewLoc       = glGetUniformLocation(program, "uView");
    const GLint uProjectionLoc = glGetUniformLocation(program, "uProjection");
    const GLint uFlatEnableLoc = glGetUniformLocation(program, "uUseFlatColor");
    const GLint uFlatColorLoc  = glGetUniformLocation(program, "uFlatColor");
    const GLint uNormalMatLoc  = glGetUniformLocation(program, "uNormalMatrix");
    const GLint uCameraPosLoc  = glGetUniformLocation(program, "uCameraPos");
    const GLint uBaseColorLoc  = glGetUniformLocation(program, "uBaseColor");
    const GLint uAmbientLoc    = glGetUniformLocation(program, "uAmbient");
    const GLint uShowNormalLoc = glGetUniformLocation(program, "uShowNormals");
    const GLint uShowTempLoc   = glGetUniformLocation(program, "uShowTemperature");

    const GLint uColormapLoc   = glGetUniformLocation(program, "uColormap");  // = element [0]
    const GLint uIsolinesLoc   = glGetUniformLocation(program, "uIsolines");
    const GLint uIsoCountLoc   = glGetUniformLocation(program, "uIsolineCount");

    bool showTemperature = true;
    bool animateWave     = false;
    int  colormapIndex   = 0;      // which entry of kColormaps is active
    bool isolines        = false;
    int  isolineCount    = 10;

    float brushRadius = 0.04f;    // in object units; every mesh is normalized to 1 unit wide
    float heatAmount  = 1.0f;     // brush temperature at its center (Step 12)

    // --- Simulation state (Steps 11-13) ---
    bool  paused           = false;
    bool  stepOnce         = false;   // set by the "Step" button, consumed next frame
    float diffusionSpeed   = 150.0f;  // k in the README formula, per second
    int   maxStepsPerFrame = 50;      // safety cap on the work done per frame
    float diffusionMs      = 0.0f;    // time the last diffusion took, for the panel

    DiffusionMethod method = DiffusionMethod::CotangentImplicit;
    float diffusivity         = 5e-4f;     // k, in (object units)^2 per second
    float diffusivityPending  = diffusivity;  // what the slider shows while dragging

    bool  wireframe    = false;
    bool  showNormals  = false;
    float baseColor[3] = {0.85f, 0.80f, 0.75f};  // light clay
    float ambient      = 0.15f;
    bool  showNeighbors = false;  // off by default: the dots are a debugging aid

    OrbitCamera camera;  // creates the object, with the default values from camera.h

    bool   rotating      = false;  // off by default now that you can rotate by hand
    float  rotationSpeed = 45.0f;  // degrees per second
    float  angle         = 0.0f;   // current rotation, in degrees
    double lastTime      = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // --- Load a new mesh if one was requested (drag & drop or "Open .obj...") ---
        // Done first thing in the frame, so nothing later in this frame is still
        // using the old mesh's vertex numbers. On failure the old mesh stays and
        // loadMeshFile() puts the reason in meshError (shown in the panel).
        if (!pendingMeshPath.empty()) {
            loadMeshFile(pendingMeshPath);
            pendingMeshPath.clear();
        }

        // Time since last frame ("delta time"). Animating with dt makes the speed
        // the same on a 60 Hz and a 144 Hz monitor.
        double now = glfwGetTime();
        float dt   = static_cast<float>(now - lastTime);
        lastTime   = now;
        if (rotating) angle += rotationSpeed * dt;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // --- UI ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- Camera input ---
        // ImGui already listens to the mouse, so we read its state instead of
        // installing our own GLFW callbacks. 'ImGuiIO&' is a reference: a second
        // name for ImGui's own object, not a copy.
        ImGuiIO& io = ImGui::GetIO();
        // WantCaptureMouse is true when the mouse is over an ImGui window, so
        // dragging a slider doesn't also spin the camera.
        if (!io.WantCaptureMouse) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                camera.rotate(io.MouseDelta.x, io.MouseDelta.y);  // pixels moved since last frame
            if (io.MouseWheel != 0.0f)
                camera.zoom(io.MouseWheel);
        }

        // --- The 3 matrices (see shaders/mesh.vert for how they're combined) ---
        // Computed here, before the UI, because picking (just below) needs them too.
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        // Model: object space -> world space. Spin around the vertical (Y) axis.
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(angle),
                                      glm::vec3(0.0f, 1.0f, 0.0f));
        // View and projection come from the camera object (see camera.cpp).
        float aspect = (h > 0) ? static_cast<float>(w) / h : 1.0f;  // h is 0 when minimized
        glm::mat4 view       = camera.viewMatrix();
        glm::mat4 projection = camera.projectionMatrix(aspect);

        // 'dirty' = the CPU copy of the temperatures changed and the GPU copy is
        // now out of date. Anything below that changes 'temperature' sets it.
        bool temperatureDirty = false;

        // --- Picking (Step 10): hold the RIGHT mouse button to heat the surface ---
        // (Left is taken by the orbit camera.)
        if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            // The inverse of the full chain object -> clip, so the ray comes out in
            // object space: the same space as mesh.positions. No need to rotate
            // 35,947 vertices; we "un-rotate" one ray instead.
            glm::mat4 invMvp = glm::inverse(projection * view * model);
            // io.MousePos and io.DisplaySize are both in window coordinates.
            Ray ray = rayFromMouse(glm::vec2(io.MousePos.x, io.MousePos.y),
                                   glm::vec2(io.DisplaySize.x, io.DisplaySize.y), invMvp);

            if (std::optional<Hit> hit = raycastMesh(mesh, ray)) {  // declare + test in one go
                lastHit = hit;
                applyHeatBrush(mesh, hit->point, hit->nearestVertex, brushRadius, heatAmount,
                               temperature);
                temperatureDirty = true;

                // Also select the vertex, so the Adjacency section and the dots follow the mouse.
                if (static_cast<int>(hit->nearestVertex) != selectedVertex) {
                    selectedVertex = static_cast<int>(hit->nearestVertex);
                    uploadHighlight(hit->nearestVertex);
                }
            }
        }

        // --- Keyboard shortcut (Step 12): Space toggles pause ---
        // WantCaptureKeyboard is true while you type in an ImGui field (e.g. the
        // vertex number), so a Space typed there doesn't also pause.
        // The 'false' = don't repeat while the key is held down.
        if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Space, false))
            paused = !paused;

        // --- Diffusion (Step 11 or Step 13, depending on the chosen method) ---
        int stepsThisFrame = 0;
        if (!paused || stepOnce) {
            // std::chrono: C++'s clock library. steady_clock never jumps (unlike
            // the wall clock), which is what you want for measuring durations.
            auto start = std::chrono::steady_clock::now();

            if (method == DiffusionMethod::GraphAverage) {
                // Step 11. How much diffusion this frame: k * dt. While paused,
                // "Step" does exactly one safe step instead.
                // dt is capped at 0.1 s: if the window freezes (e.g. while you drag
                // it), the next frame shouldn't try to catch up on a whole second.
                const float amount = paused ? kMaxStepAmount
                                            : diffusionSpeed * std::min(dt, 0.1f);
                stepsThisFrame = diffuse(mesh, temperature, scratch, amount, maxStepsPerFrame);
            } else {
                // Step 13. Fixed time step: the matrix is factorized for one step
                // duration (kImplicitStep), so we always step by exactly that much.
                // The accumulator collects real time and pays it out in whole steps:
                //   frame of 16.7 ms -> 1 step;  frame of 40 ms -> 2 steps, 6.7 ms left over.
                if (paused) {
                    implicitAccumulator = kImplicitStep;  // "Step" = exactly one step
                } else {
                    implicitAccumulator += std::min(dt, 0.1f);
                }
                while (implicitAccumulator >= kImplicitStep && stepsThisFrame < 4) {
                    if (!heatSolver.step(temperature, diffusivity * kImplicitStep)) break;
                    implicitAccumulator -= kImplicitStep;
                    ++stepsThisFrame;
                }
                // Too far behind (4 steps weren't enough): drop the backlog instead
                // of letting it grow forever.
                if (implicitAccumulator >= kImplicitStep) implicitAccumulator = 0.0f;
            }

            auto end = std::chrono::steady_clock::now();
            // duration<float, std::milli> = "a duration, as a float, in milliseconds".
            diffusionMs = std::chrono::duration<float, std::milli>(end - start).count();

            if (stepsThisFrame > 0) temperatureDirty = true;
            stepOnce = false;  // a single step is used up
        }

        // Statistics shown in the panel. The sums are doubles: adding 35,947 floats
        // into a float loses precision as the total grows.
        float  maxT      = 0.0f;
        double sumT      = 0.0;
        for (float t : temperature) {
            maxT  = std::max(maxT, t);
            sumT += t;
        }
        // Integral of T over the surface (sum of area_i * T_i): the physically
        // meaningful "amount of heat". Step 13 conserves it exactly.
        const double integralT = heatSolver.totalHeat(temperature);

        ImGui::Begin("HeatMesh");
        ImGui::Text("%.1f FPS", io.Framerate);

        // ===== Mesh: which .obj is loaded, and loading another one =====
        ImGui::SeparatorText("Mesh");
        ImGui::TextUnformatted(meshName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu vertices, %zu triangles)",
                            mesh.vertexCount(), mesh.triangleCount());
        if (ImGui::Button("Open .obj...")) {
            // Opens the system's file dialog. .result() *waits* until the user picks
            // a file or cancels; the window doesn't redraw in the meantime.
            // make_preferred() turns '/' into '\' on Windows, which the dialog wants.
            const std::string startFolder =
                std::filesystem::path(PROJECT_DIR "/assets").make_preferred().string();
            std::vector<std::string> files =
                pfd::open_file("Open a mesh", startFolder,
                               {"OBJ meshes (*.obj)", "*.obj", "All files", "*"})
                    .result();
            if (!files.empty()) pendingMeshPath = files[0];  // empty = cancelled
        }
        ImGui::SameLine();
        if (ImGui::Button("Bunny")) pendingMeshPath = PROJECT_DIR "/assets/bunny.obj";
        ImGui::SameLine();
        ImGui::TextDisabled("or drop a .obj on the window");
        if (!meshError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Could not load: %s",
                               meshError.c_str());

        // ===== Simulation (Steps 11-12): always visible, used the most =====
        ImGui::SeparatorText("Simulation");
        if (ImGui::Button(paused ? "Play " : "Pause")) paused = !paused;  // label follows the state
        ImGui::SameLine();
        ImGui::BeginDisabled(!paused);  // greyed out and unclickable while running
        if (ImGui::Button("Step")) stepOnce = true;  // takes effect next frame (see above)
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            std::fill(temperature.begin(), temperature.end(), 0.0f);
            temperatureDirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(Space: pause)");

        // Method choice (Step 13). RadioButton(label, active) returns true when clicked.
        if (ImGui::RadioButton("Graph average (11)", method == DiffusionMethod::GraphAverage))
            method = DiffusionMethod::GraphAverage;
        ImGui::SameLine();
        if (ImGui::RadioButton("Cotangent + implicit (13)",
                               method == DiffusionMethod::CotangentImplicit))
            method = DiffusionMethod::CotangentImplicit;

        if (method == DiffusionMethod::GraphAverage) {
            // Logarithmic: 1..10, 10..100, 100..1000 each take the same slider width,
            // so you can be precise at low speeds and still reach high ones.
            ImGui::SliderFloat("Diffusion speed", &diffusionSpeed, 1.0f, 3000.0f, "%.0f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderInt("Max steps / frame", &maxStepsPerFrame, 1, 200);
        } else {
            // The slider edits a *pending* value. Changing the real diffusivity means
            // re-factorizing the matrix (~0.25 s), so we only apply it when the
            // slider is released, not on every frame of the drag.
            ImGui::SliderFloat("Diffusivity", &diffusivityPending, 1e-5f, 1e-1f, "%.1e",
                               ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemDeactivatedAfterEdit())  // "the widget just above" was released
                diffusivity = diffusivityPending;
            ImGui::TextDisabled("units^2/s, applied on release (re-factorizes: %.0f ms)",
                                heatSolver.lastFactorMs());
        }

        if (paused) {
            ImGui::TextDisabled("Paused");
        } else {
            ImGui::Text("%d steps this frame, %.2f ms", stepsThisFrame, diffusionMs);
            if (method == DiffusionMethod::GraphAverage && stepsThisFrame >= maxStepsPerFrame)
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                                   "Step limit reached: running slower than real time");
        }
        ImGui::Text("Max T: %.3f", maxT);
        // %.6e: scientific notation, e.g. 4.334570e-04, so tiny changes are visible.
        ImGui::Text("Sum of T: %.4f   Integral T dA: %.6e", sumT, integralT);

        // ===== Heat brush (Steps 10 + 12) =====
        ImGui::SeparatorText("Heat brush");
        ImGui::TextDisabled("Right-click / right-drag on the mesh to heat it");
        ImGui::SliderFloat("Heat amount", &heatAmount, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Radius", &brushRadius, 0.0f, 0.15f, "%.3f");
        if (lastHit) {
            ImGui::Text("Last hit: vertex %u, T = %.3f", lastHit->nearestVertex,
                        temperature[lastHit->nearestVertex]);
        } else {
            ImGui::TextDisabled("No hit yet");
        }

        // ===== Colormap (Step 9) =====
        ImGui::SeparatorText("Colormap");
        // BeginCombo/EndCombo: a drop-down list. The preview text is the current name.
        if (ImGui::BeginCombo("Colormap", kColormaps[colormapIndex].name)) {
            for (int i = 0; i < static_cast<int>(kColormaps.size()); ++i) {
                // Selectable returns true when clicked; the 2nd argument highlights the current one.
                if (ImGui::Selectable(kColormaps[i].name, i == colormapIndex))
                    colormapIndex = i;
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Show temperature", &showTemperature);
        ImGui::SameLine();
        ImGui::Checkbox("Isolines", &isolines);
        if (isolines) ImGui::SliderInt("Bands", &isolineCount, 2, 30);
        drawColormapLegend(kColormaps[colormapIndex]);

        // ===== Everything else: collapsible, closed by default =====
        // CollapsingHeader returns true while the section is open.
        if (ImGui::CollapsingHeader("Display")) {
            ImGui::ColorEdit3("Background", clearColor);
            ImGui::Checkbox("Wireframe", &wireframe);
            ImGui::Checkbox("Rotate", &rotating);
            ImGui::SliderFloat("Speed (deg/s)", &rotationSpeed, 0.0f, 360.0f);
        }

        if (ImGui::CollapsingHeader("Lighting")) {
            ImGui::ColorEdit3("Base color", baseColor);
            ImGui::SliderFloat("Ambient", &ambient, 0.0f, 1.0f);
            ImGui::Checkbox("Show normals", &showNormals);
        }

        if (ImGui::CollapsingHeader("Camera")) {
            ImGui::TextDisabled("Left-drag: orbit   Scroll: zoom");
            ImGui::Text("yaw %.0f  pitch %.0f  dist %.2f",
                        camera.yaw(), camera.pitch(), camera.distance());
            ImGui::SliderFloat("FOV", &camera.fovDegrees, 20.0f, 100.0f);
            if (ImGui::Button("Reset camera")) camera.reset();  // Button returns true when clicked
        }

        if (ImGui::CollapsingHeader("Adjacency")) {
            ImGui::Text("Neighbours per vertex: min %zu, max %zu, avg %.2f",
                        minNeighbors, maxNeighbors, avgNeighbors);
            ImGui::Checkbox("Show neighbours", &showNeighbors);
            // InputInt returns true on the frame the value changes.
            bool changed = ImGui::InputInt("Vertex", &selectedVertex);
            if (ImGui::Button("Random vertex")) {
                selectedVertex = std::rand();  // 0..RAND_MAX; clamped just below
                changed = true;
            }
            if (changed) {
                // Keep the index valid: 0..vertexCount-1 (the % wraps big random numbers).
                const int n = static_cast<int>(mesh.vertexCount());
                selectedVertex = ((selectedVertex % n) + n) % n;  // also wraps -1 to n-1
                uploadHighlight(static_cast<unsigned int>(selectedVertex));
            }
            // Print the neighbour list: "12 -> 7 9 13 ..."
            ImGui::TextWrapped("%d -> %s", selectedVertex,
                               neighborListText(mesh.neighbors[selectedVertex]).c_str());
            ImGui::Text("T[%d] = %.3f", selectedVertex, temperature[selectedVertex]);
        }

        if (ImGui::CollapsingHeader("Test patterns")) {
            if (ImGui::Button("Height gradient")) {
                fillHeightGradient(mesh, temperature);
                temperatureDirty = true;
            }
            // The wave overwrites every temperature each frame, so diffusion has
            // no visible effect while it's on.
            ImGui::Checkbox("Animate wave", &animateWave);
        }
        if (animateWave) {
            fillWave(mesh, static_cast<float>(now), temperature);
            temperatureDirty = true;  // new values every frame -> upload every frame
        }

        if (temperatureDirty) uploadTemperatures();
        ImGui::End();

        // --- Render ---
        glViewport(0, 0, w, h);
        glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Depth test: keep a pixel only if it's closer than what's already there.
        // Without it, faces are drawn in index order and back faces can cover front ones.
        // (Enabled every frame because ImGui's renderer toggles GL state too.)
        glEnable(GL_DEPTH_TEST);

        // Draw the mesh (before the UI, so the UI draws on top)
        glUseProgram(program);
        // Wireframe lines are drawn in flat grey. GLSL bools are set as ints (0/1).
        glUniform1i(uFlatEnableLoc, wireframe);
        glUniform3f(uFlatColorLoc, 0.9f, 0.9f, 0.9f);
        // glm::value_ptr gives a float* to the 16 matrix values, in the column-major
        // order OpenGL expects (so the 'transpose' argument is GL_FALSE).
        glUniformMatrix4fv(uModelLoc,      1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(uViewLoc,       1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(uProjectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

        // Normals are directions, not points, so they're transformed with the
        // inverse-transpose of the model matrix's 3x3 part. For pure rotations this
        // equals the rotation itself, but it stays correct if you later add a
        // non-uniform scale (e.g. stretch x by 2), where using 'model' would tilt
        // the normals off the surface.
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        glUniformMatrix3fv(uNormalMatLoc, 1, GL_FALSE, glm::value_ptr(normalMatrix));

        glm::vec3 cameraPos = camera.position();
        glUniform3fv(uCameraPosLoc, 1, glm::value_ptr(cameraPos));
        glUniform3fv(uBaseColorLoc, 1, baseColor);  // a float[3] already is a float*
        glUniform1f(uAmbientLoc, ambient);
        glUniform1i(uShowNormalLoc, showNormals);
        glUniform1i(uShowTempLoc, showTemperature);
        // Upload all 5 stops in one call: count = 5 vec3s, read from consecutive
        // memory starting at stops[0] (std::array is contiguous, like a C array).
        glUniform3fv(uColormapLoc, kColormapStops,
                     glm::value_ptr(kColormaps[colormapIndex].stops[0]));
        glUniform1i(uIsolinesLoc, isolines);
        glUniform1f(uIsoCountLoc, static_cast<float>(isolineCount));

        // GL_LINE draws only triangle edges; GL_FILL is the normal solid mode.
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

        glBindVertexArray(vao);
        // Draw 'indexCount' indices, read from the EBO bound in this VAO, as triangles.
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, (void*)0);

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);  // back to normal so the UI isn't wireframe

        // --- Neighbour highlight: selected vertex in red, its neighbours in yellow ---
        if (showNeighbors) {
            // Without the depth test, the dots show through the surface, so you
            // can still see them when they're on the far side of the bunny.
            glDisable(GL_DEPTH_TEST);
            glUniform1i(uFlatEnableLoc, 1);
            glBindVertexArray(highlightVao);

            // Neighbours: indices 1..count-1. The last argument is a *byte offset*
            // into the index buffer: skip the first index (4 bytes).
            glPointSize(8.0f);
            glUniform3f(uFlatColorLoc, 1.0f, 0.85f, 0.1f);
            glDrawElements(GL_POINTS, highlightCount - 1, GL_UNSIGNED_INT,
                           (void*)sizeof(unsigned int));

            // Selected vertex: just index 0, drawn last so it's on top.
            glPointSize(14.0f);
            glUniform3f(uFlatColorLoc, 1.0f, 0.15f, 0.15f);
            glDrawElements(GL_POINTS, 1, GL_UNSIGNED_INT, (void*)0);

            glEnable(GL_DEPTH_TEST);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Free GPU resources (OpenGL objects aren't freed automatically like C++ locals).
    glDeleteVertexArrays(1, &vao);
    glDeleteVertexArrays(1, &highlightVao);
    glDeleteBuffers(1, &highlightEbo);
    glDeleteBuffers(1, &positionVbo);
    glDeleteBuffers(1, &normalVbo);
    glDeleteBuffers(1, &temperatureVbo);
    glDeleteBuffers(1, &ebo);
    glDeleteProgram(program);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
