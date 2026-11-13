// HeatMesh: real-time heat diffusion on a 3D mesh.
// main.cpp owns the window, the main loop and (for now) the scene setup.

#include <glad/gl.h>      // must come before GLFW
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>                    // vec3, mat4, ...
#include <glm/gtc/matrix_transform.hpp>   // rotate, lookAt, perspective
#include <glm/gtc/type_ptr.hpp>           // value_ptr (matrix -> float* for OpenGL)

#include <algorithm>  // std::min, std::max
#include <cstdint>    // SIZE_MAX
#include <cstdio>
#include <cstdlib>    // std::rand
#include <string>
#include <vector>

#include "camera.h"
#include "mesh.h"
#include "shader.h"  // our own header: quotes = "look in my project first"

// Prints OpenGL errors as they happen (Windows/Linux drivers; macOS doesn't support it).
static void GLAPIENTRY onGlDebug(GLenum, GLenum type, GLuint, GLenum severity,
                                 GLsizei, const GLchar* message, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;  // skip info spam
    std::fprintf(stderr, "[GL %s] %s\n",
                 type == GL_DEBUG_TYPE_ERROR ? "ERROR" : "warn", message);
}

// Turns {7, 9, 13} into the text "7 9 13" for display.
static std::string neighborListText(const std::vector<unsigned int>& list) {
    std::string text;
    for (unsigned int v : list) {
        text += std::to_string(v);  // number -> text
        text += ' ';
    }
    return text;
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

    // --- Mesh setup (done once, before the loop) ---
    // PROJECT_DIR comes from CMakeLists.txt (target_compile_definitions), so the
    // asset and shader files are found no matter which folder the .exe runs from.
    GLuint program = loadShaderProgram(PROJECT_DIR "/shaders/mesh.vert",
                                       PROJECT_DIR "/shaders/mesh.frag");
    if (!program) return 1;

    Mesh mesh;
    if (!loadObj(PROJECT_DIR "/assets/bunny.obj", mesh)) return 1;
    normalizeToUnitBox(mesh);
    computeVertexNormals(mesh);  // after normalizing, so normals match the final shape
    buildAdjacency(mesh);

    // Neighbour-count statistics, shown in the UI. Most vertices of a typical
    // triangle mesh have ~6 neighbours; vertices on a hole's border have fewer.
    size_t minNeighbors = SIZE_MAX, maxNeighbors = 0, totalNeighbors = 0;
    for (const auto& list : mesh.neighbors) {
        minNeighbors    = std::min(minNeighbors, list.size());
        maxNeighbors    = std::max(maxNeighbors, list.size());
        totalNeighbors += list.size();
    }
    const float avgNeighbors = static_cast<float>(totalNeighbors) / mesh.vertexCount();
    std::printf("Adjacency: %zu..%zu neighbours per vertex, average %.2f\n",
                minNeighbors, maxNeighbors, avgNeighbors);

    const GLsizei indexCount = static_cast<GLsizei>(mesh.indices.size());

    // Same VAO/VBO/EBO idea as the cube, but the data now comes from std::vectors.
    // .data() gives a pointer to the vector's first element (its storage is one
    // contiguous block, exactly like a C array), and the byte size is
    // element count * bytes per element.
    //
    // New in Step 6: positions and normals live in two *separate* VBOs instead of
    // one interleaved buffer like the cube had. Both layouts work; separate buffers
    // let us later update one attribute (the temperature, Step 8) without touching
    // the others.
    GLuint vao = 0, positionVbo = 0, normalVbo = 0, ebo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &positionVbo);
    glGenBuffers(1, &normalVbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    // Attribute 0 (aPos): 3 floats. A glm::vec3 is exactly 3 packed floats,
    // so the stride is sizeof(glm::vec3) = 12 bytes, offset 0.
    // glVertexAttribPointer records *whichever buffer is bound right now*, so
    // bind first, then describe.
    glBindBuffer(GL_ARRAY_BUFFER, positionVbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.positions.size() * sizeof(glm::vec3),
                 mesh.positions.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);

    // Attribute 1 (aNormal): same layout, from the second buffer.
    glBindBuffer(GL_ARRAY_BUFFER, normalVbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.normals.size() * sizeof(glm::vec3),
                 mesh.normals.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int),
                 mesh.indices.data(), GL_STATIC_DRAW);

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

    int selectedVertex = 0;           // int, because ImGui::InputInt works with int
    GLsizei highlightCount = 0;       // how many indices are in highlightEbo

    // Lambda that refills highlightEbo for a given vertex. Defined here, called
    // now and again whenever the selection changes.
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
    uploadHighlight(selectedVertex);

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

    bool  wireframe    = false;
    bool  showNormals  = false;
    float baseColor[3] = {0.85f, 0.80f, 0.75f};  // light clay
    float ambient      = 0.15f;
    bool  showNeighbors = true;

    OrbitCamera camera;  // creates the object, with the default values from camera.h

    bool   rotating      = false;  // off by default now that you can rotate by hand
    float  rotationSpeed = 45.0f;  // degrees per second
    float  angle         = 0.0f;   // current rotation, in degrees
    double lastTime      = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

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

        ImGui::Begin("HeatMesh");
        ImGui::Text("%.1f FPS", io.Framerate);
        ImGui::Text("%zu vertices, %zu triangles", mesh.vertexCount(), mesh.triangleCount());
        ImGui::ColorEdit3("Background", clearColor);
        ImGui::Checkbox("Wireframe", &wireframe);
        ImGui::Checkbox("Rotate", &rotating);
        ImGui::SliderFloat("Speed (deg/s)", &rotationSpeed, 0.0f, 360.0f);

        ImGui::SeparatorText("Lighting");
        ImGui::ColorEdit3("Base color", baseColor);
        ImGui::SliderFloat("Ambient", &ambient, 0.0f, 1.0f);
        ImGui::Checkbox("Show normals", &showNormals);

        ImGui::SeparatorText("Camera");
        ImGui::TextDisabled("Left-drag: orbit   Scroll: zoom");
        ImGui::Text("yaw %.0f  pitch %.0f  dist %.2f",
                    camera.yaw(), camera.pitch(), camera.distance());
        ImGui::SliderFloat("FOV", &camera.fovDegrees, 20.0f, 100.0f);
        if (ImGui::Button("Reset camera")) camera.reset();  // Button returns true when clicked

        ImGui::SeparatorText("Adjacency");
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
        ImGui::End();

        // --- Render ---
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Depth test: keep a pixel only if it's closer than what's already there.
        // Without it, faces are drawn in index order and back faces can cover front ones.
        // (Enabled every frame because ImGui's renderer toggles GL state too.)
        glEnable(GL_DEPTH_TEST);

        // --- The 3 matrices (see shaders/mesh.vert for how they're combined) ---
        // Model: object space -> world space. Spin around the vertical (Y) axis.
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(angle),
                                      glm::vec3(0.0f, 1.0f, 0.0f));
        // View and projection now come from the camera object (see camera.cpp).
        float aspect = (h > 0) ? static_cast<float>(w) / h : 1.0f;  // h is 0 when minimized
        glm::mat4 view       = camera.viewMatrix();
        glm::mat4 projection = camera.projectionMatrix(aspect);

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
    glDeleteBuffers(1, &ebo);
    glDeleteProgram(program);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
