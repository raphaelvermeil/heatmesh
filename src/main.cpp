// HeatMesh — starter: opens a window, loads OpenGL, shows an ImGui panel.
// Nothing 3D is drawn yet; that's your first LearnOpenGL step (Hello Triangle).

#include <glad/gl.h>      // must come before GLFW
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>

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

        // TODO: your mesh drawing goes here (before the UI, so the UI draws on top)

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
