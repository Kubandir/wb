#ifndef FILE_OPS_H
#define FILE_OPS_H

#include "editor.h"
#include <string>

bool loadFile(Editor& editor, const std::string& filepath);
bool saveFile(Editor& editor, const std::string& filepath);

#endif
