#ifndef EDITOR_H
#define EDITOR_H

#include <vector>
#include <string>
#include <deque>
#include <set>
#include <ctime>

struct UndoState {
    std::vector<std::string> lines;
    int cursor_y;
    int cursor_x;
};

struct Notification {
    std::string message;
    time_t shown_at;
    bool active;
};

struct SegmentInfo {
    int logical_line;
    int char_start;
    int char_end;
    bool is_continuation;
    int visual_indent;
};

struct SearchState {
    bool active;
    std::string query;
    int match_part;
    int match_line;
    int match_col;
};

struct Part {
    std::string name;
    std::vector<std::string> lines;
    int color_idx;
    std::set<int> folded_lines;
};

enum class TabBarMode { NORMAL, RENAMING };

struct TabPosition {
    int idx;
    int x_start;
    int x_end;
};

struct TabBar {
    int scroll_offset;
    TabBarMode mode;
    std::string input_buf;
    int rename_idx;
    bool focused;
    int cursor;
    std::vector<TabPosition> tab_positions;
};

struct Editor {
    std::vector<Part> parts;
    int active_part;
    bool reading_mode;
    int reading_scroll;
    TabBar tabbar;

    std::vector<std::vector<std::string>> last_saved_lines;
    int cursor_y;
    int cursor_x;
    int scroll_offset;
    bool needs_redraw;
    std::string filepath;
    std::deque<UndoState> undo_stack;
    std::deque<UndoState> redo_stack;
    Notification notification;
    std::vector<SegmentInfo> last_segments;
    SearchState search;
    std::string last_typed_word;
    int preferred_x;
    bool segments_dirty;

    std::vector<std::string>& lines() { return parts[active_part].lines; }
    const std::vector<std::string>& lines() const { return parts[active_part].lines; }
};

// color_idx -> ncurses color pair number
int partColorPair(int color_idx);
int partColorCount();

void initEditor(Editor& editor);
bool isBulletLine(const std::string& line);
void pushUndo(Editor& editor);
void pushUndoIfWordBoundary(Editor& editor, char ch);
void applyUndo(Editor& editor);
void applyRedo(Editor& editor);
void setNotification(Editor& editor, const std::string& message);

#endif
