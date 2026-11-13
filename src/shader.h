// shader.h: loading GLSL shader files and building an OpenGL program from them.
//
// A header (.h) holds *declarations*: "this function exists, here's its signature".
// The matching .cpp holds the *definitions* (the actual code). Any file that wants
// to call these functions does #include "shader.h"; the linker connects the calls
// to the code compiled from shader.cpp.

#pragma once  // include this file at most once per .cpp, even if #included twice

#include <glad/gl.h>

#include <string>

// Reads a whole text file into a string. Returns false if it can't be opened.
// 'out' is passed by reference (&): the function writes directly into the
// caller's variable instead of a copy.
bool readTextFile(const std::string& path, std::string& out);

// Loads, compiles and links a vertex + fragment shader pair.
// Returns the program id, or 0 on failure (errors are printed to stderr).
GLuint loadShaderProgram(const std::string& vertexPath, const std::string& fragmentPath);
