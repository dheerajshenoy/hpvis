#pragma once

#include <QString>

// Reads a GLSL file from the Qt resource system and expands
// `#include "file"` lines (relative to the including file, each file at most
// once). Returns an empty string and prints an error if a file is missing.
QString loadShaderSource(const QString &resourcePath);
