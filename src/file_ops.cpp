#include "file_ops.h"
#include <fstream>
#include <algorithm>

bool loadFile(Editor& editor, const std::string& filepath) {
    std::ifstream file(filepath);
    editor.filepath = filepath;
    if(!file.is_open()) return false;

    editor.parts.clear();
    std::string line;
    Part current;
    current.name = "Part 1";
    current.color_idx = 0;
    current.folded_lines = {};
    bool has_markers = false;

    while(std::getline(file, line)) {
        if(line.size() > 4 && line.substr(0, 2) == "<<" && line.back() == '>' && line[line.size()-2] == '>') {
            if(has_markers || !current.lines.empty()) {
                if(current.lines.empty()) current.lines.push_back("");
                editor.parts.push_back(current);
            }
            has_markers = true;
            std::string inner = line.substr(2, line.size() - 4);
            // Parse optional :colorIdx suffix
            current.color_idx = 0;
            current.folded_lines = {};
            size_t colon = inner.rfind(':');
            if(colon != std::string::npos) {
                std::string suffix = inner.substr(colon + 1);
                bool all_digits = !suffix.empty();
                for(char c : suffix) if(!std::isdigit((unsigned char)c)) { all_digits = false; break; }
                if(all_digits) {
                    current.color_idx = std::stoi(suffix);
                    inner = inner.substr(0, colon);
                }
            }
            current.name = inner;
            current.lines.clear();
        } else {
            current.lines.push_back(line);
        }
    }
    if(current.lines.empty()) current.lines.push_back("");
    editor.parts.push_back(current);
    file.close();

    if(editor.parts.empty()) editor.parts.push_back({"Part 1", {""}, 0, {}});
    editor.last_saved_lines.clear();
    for(auto& p : editor.parts) editor.last_saved_lines.push_back(p.lines);
    editor.active_part = 0;
    editor.cursor_y = 0;
    editor.cursor_x = 0;
    editor.scroll_offset = 0;
    return true;
}

bool saveFile(Editor& editor, const std::string& filepath) {
    std::ofstream file(filepath, std::ios::out | std::ios::trunc);
    if(!file.is_open()) return false;
    for(int p = 0; p < (int)editor.parts.size(); p++) {
        file << "<<" << editor.parts[p].name << ":" << editor.parts[p].color_idx << ">>\n";
        const auto& lines = editor.parts[p].lines;
        for(size_t i = 0; i < lines.size(); i++) {
            file << lines[i];
            if(i < lines.size() - 1) file << '\n';
        }
        if(p < (int)editor.parts.size() - 1) file << '\n';
    }
    file.close();
    editor.last_saved_lines.clear();
    for(auto& p : editor.parts) editor.last_saved_lines.push_back(p.lines);
    return true;
}
