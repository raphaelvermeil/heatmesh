// HeatMesh — starter: opens a window, loads OpenGL, shows an ImGui panel.
// Nothing 3D is drawn yet; that's your first LearnOpenGL step (Hello Triangle).

#include <glad/gl.h>      // must come before GLFW
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>

// --- Step 1: Hello Triangle --------------------------------------------------
// Shaders are small programs that run on the GPU. They're written in GLSL
// (a C-like language) and compiled at runtime by the graphics driver.
// For now they live in C++ raw string literals: R"( ... )" lets us write
// multi-line text without escaping quotes or newlines. Step 2 moves them to files.

// Vertex shader: runs once per vertex. Its job is to output gl_Position,
// the vertex's position in "clip space" (x, y, z each from -1 to 1 are visible).
static const char* kVertexShaderSrc = R"(
#version 410 core
layout(location = 0) in vec3 aPos;    // attribute 0: position (read from the VBO)
layout(location = 1) in vec3 aColor;  // attribute 1: color

out vec3 vColor;  // passed on to the fragment shader

void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 1.0);
}
)";

// Fragment shader: runs once per pixel covered by the triangle. vColor is
// automatically interpolated between the 3 vertices, giving a smooth gradient.
static const char* kFragmentShaderSrc = R"(
#version 410 core
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

// Compiles one shader stage. Returns its OpenGL id (a plain unsigned int handle),
// or 0 on failure. 'static' here means "only visible inside this .cpp file".
static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error:\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Links a vertex + fragment shader into a "program" that the GPU can run.
static GLuint createProgram(const char* vertexSrc, const char* fragmentSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!vs || !fs) return 0;

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    // Once linked, the individual shader objects are no longer needed.
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error:\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

// Prints OpenGL errors as they happen (Windows/Linux drivers; macOS doesn't support it).
static void GLAPIENTRY onGlDebug(GLenum, GLenum type, GLuint, GLenum severity,
                                 GLsizei, const GLchar* message, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;  // skip info spam
    std::fprintf(stderr, "[GL %s] %s\n",
                 type == GL_DEBUG_TYPE_ERROR ? "ERROR" : "warn", message);
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

    // --- Triangle setup (done once, before the loop) ---
    GLuint program = createProgram(kVertexShaderSrc, kFragmentShaderSrc);
    if (!program) return 1;

    // Vertex data: 3 vertices, each = 3 floats of position + 3 floats of color.
    // This is "interleaved" data: x y z r g b | x y z r g b | ...
    float vertices[] = {
        // position            // color
        -0.5f, -0.5f, 0.0f,    1.0f, 0.0f, 0.0f,  // bottom left: red
         0.5f, -0.5f, 0.0f,    0.0f, 1.0f, 0.0f,  // bottom right: green
         0.0f,  0.5f, 0.0f,    0.0f, 0.0f, 1.0f,  // top: blue
    };

    // VAO (Vertex Array Object): remembers *how* to read vertex data
    // (which buffer, which attributes, what layout). Bind it, then configure.
    // VBO (Vertex Buffer Object): the raw bytes, copied into GPU memory.
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);   // &vao = "address of vao", so GL can write the new id into it
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Attribute 0 (aPos): 3 floats, stride = 6 floats between vertices, starts at byte 0.
    const GLsizei stride = 6 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    // Attribute 1 (aColor): 3 floats, starts after the 3 position floats.
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);  // unbind so later code can't accidentally modify it

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // --- UI ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("HeatMesh");
        ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
        ImGui::ColorEdit3("Background", clearColor);
        ImGui::End();

        // --- Render ---
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Draw the triangle (before the UI, so the UI draws on top)
        glUseProgram(program);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);  // 3 vertices starting at index 0

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Free GPU resources (OpenGL objects aren't freed automatically like C++ locals).
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
