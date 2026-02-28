#include "editor.h"
#include "render.h"

static const int MAX_UNDO = 200;

// Cycles: blue, purple, red, orange, yellow, green, cyan, white
static const int PART_COLOR_PAIRS[] = { CP_PART_BASE, CP_PART_BASE+1, CP_PART_BASE+2, CP_PART_BASE+3,
                                         CP_PART_BASE+4, CP_PART_BASE+5, CP_PART_BASE+6, CP_PART_BASE+7 };

int partColorCount() { return 8; }
int partColorPair(int idx) { return PART_COLOR_PAIRS[((idx % 8) + 8) % 8]; }

void initEditor(Editor& editor) {
    editor.parts.push_back({"Part 1", {""}, 0, {}});
    editor.active_part = 0;
    editor.reading_mode = false;
    editor.reading_scroll = 0;
    editor.tabbar = {0, TabBarMode::NORMAL, "", -1, false, 0};
    editor.cursor_y = 0;
    editor.cursor_x = 0;
    editor.scroll_offset = 0;
    editor.needs_redraw = true;
    editor.filepath = "";
    editor.notification = {"", 0, false};
    editor.search = {false, "", -1, -1, -1};
    editor.last_typed_word = "";
    editor.preferred_x = 0;
    editor.segments_dirty = true;
}

void setNotification(Editor& editor, const std::string& message) {
    editor.notification = {message, time(nullptr), true};
    editor.needs_redraw = true;
}

bool isBulletLine(const std::string& line) {
    size_t i = 0;
    while(i < line.length() && line[i] == ' ') i++;
    return i < line.length() && line[i] == '-';
}

void pushUndo(Editor& editor) {
    editor.redo_stack.clear();
    editor.undo_stack.push_back({editor.lines(), editor.cursor_y, editor.cursor_x});
    if((int)editor.undo_stack.size() > MAX_UNDO)
        editor.undo_stack.pop_front();
    editor.last_typed_word = "";
}

void pushUndoIfWordBoundary(Editor& editor, char ch) {
    bool is_boundary = (ch == ' ' || ch == '\t' || ch == '.' || ch == ',' ||
                        ch == '!' || ch == '?' || ch == ':' || ch == ';' ||
                        ch == '(' || ch == ')' || ch == '"' || ch == '\'');
    if(is_boundary && !editor.last_typed_word.empty())
        pushUndo(editor);
    else if(!is_boundary)
        editor.last_typed_word += ch;
}

void applyUndo(Editor& editor) {
    if(editor.undo_stack.empty()) return;
    editor.redo_stack.push_back({editor.lines(), editor.cursor_y, editor.cursor_x});
    UndoState& s = editor.undo_stack.back();
    editor.lines() = s.lines;
    editor.cursor_y = s.cursor_y;
    editor.cursor_x = s.cursor_x;
    editor.undo_stack.pop_back();
    editor.last_typed_word = "";
    editor.segments_dirty = true;
    editor.needs_redraw = true;
}

void applyRedo(Editor& editor) {
    if(editor.redo_stack.empty()) return;
    editor.undo_stack.push_back({editor.lines(), editor.cursor_y, editor.cursor_x});
    UndoState& s = editor.redo_stack.back();
    editor.lines() = s.lines;
    editor.cursor_y = s.cursor_y;
    editor.cursor_x = s.cursor_x;
    editor.redo_stack.pop_back();
    editor.last_typed_word = "";
    editor.segments_dirty = true;
    editor.needs_redraw = true;
}
