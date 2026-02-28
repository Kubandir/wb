#ifndef RENDER_H
#define RENDER_H

#include "editor.h"
#include <vector>
#include <string>

#define KEY_CTRL_LEFT  1001
#define KEY_CTRL_RIGHT 1002

#define CP_ACCENT       1
#define CP_SEARCH_HL    2
#define CP_DIM          3
#define CP_READING      4
#define CP_H1           5
#define CP_H2           6
#define CP_H3           7
#define CP_SUBTLE       8
#define CP_PART_BASE    9 

void initRender();
void renderEditor(Editor& editor);
std::vector<SegmentInfo> buildSegments(const std::vector<std::string>& lines, int max_x,
                                        const std::set<int>* folded = nullptr, int cursor_line = -1);

#endif
