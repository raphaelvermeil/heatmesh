// shader.cpp: definitions for the functions declared in shader.h.

#include "shader.h"

#include <cstdio>
#include <fstream>   // std::ifstream: read files
#include <sstream>   // std::stringstream: build a string in memory

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream file(path);  // opens the file; closes automatically at the end of the scope
    if (!file) return false;   // a stream converts to false if opening failed

    std::stringstream buffer;
    buffer << file.rdbuf();    // copy the entire file contents into the buffer
    out = buffer.str();
    return true;
}

// 'static' = private to this file: main.cpp can't (and doesn't need to) call it.
static GLuint compileShader(GLenum type, const std::string& source, const std::string& path) {
    GLuint shader = glCreateShader(type);
    // OpenGL is a C API, so it wants a const char*, not a std::string. c_str() gives one.
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error in %s:\n%s\n", path.c_str(), log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint loadShaderProgram(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vertexSrc, fragmentSrc;
    if (!readTextFile(vertexPath, vertexSrc)) {
        std::fprintf(stderr, "Can't open shader file: %s\n", vertexPath.c_str());
        return 0;
    }
    if (!readTextFile(fragmentPath, fragmentSrc)) {
        std::fprintf(stderr, "Can't open shader file: %s\n", fragmentPath.c_str());
        return 0;
    }

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSrc, vertexPath);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc, fragmentPath);
    if (!vs || !fs) {
        // glDeleteShader(0) is allowed and does nothing, so this is safe either way.
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

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
        std::fprintf(stderr, "Program link error (%s + %s):\n%s\n",
                     vertexPath.c_str(), fragmentPath.c_str(), log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}
