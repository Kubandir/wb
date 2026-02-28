#include "input.h"
#include "editor.h"
#include "file_ops.h"
#include "render.h"
#include "util.h"
#include <ncurses.h>
#include <algorithm>
#include <cctype>
#include <sstream>

static int lineNumWidth(int n) {
    int w = 1;
    while(n >= 10) { n /= 10; w++; }
    return w;
}

static void rebuildSegmentsIfDirty(Editor& editor) {
    if(!editor.segments_dirty) return;
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_y;
    int lnw = lineNumWidth((int)editor.lines().size());
    int text_w = max_x - lnw - 1;
    if(text_w < 4) text_w = max_x;
    editor.last_segments = buildSegments(editor.lines(), text_w, &editor.parts[editor.active_part].folded_lines);
    editor.segments_dirty = false;
}

static void scrollToCursor(Editor& editor) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_x;
    int content_rows = max_y - 2;
    rebuildSegmentsIfDirty(editor);
    const auto& segs = editor.last_segments;
    for(int i = 0; i < (int)segs.size(); i++) {
        if(segs[i].logical_line == editor.cursor_y &&
           editor.cursor_x >= segs[i].char_start && editor.cursor_x <= segs[i].char_end) {
            if(i < editor.scroll_offset) editor.scroll_offset = i;
            else if(i >= editor.scroll_offset + content_rows) editor.scroll_offset = i - content_rows + 1;
            return;
        }
    }
}

static void cursorUp(Editor& editor) {
    rebuildSegmentsIfDirty(editor);
    const auto& segs = editor.last_segments;
    for(int i = 0; i < (int)segs.size(); i++) {
        if(segs[i].logical_line == editor.cursor_y &&
           editor.cursor_x >= segs[i].char_start && editor.cursor_x <= segs[i].char_end) {
            if(i == 0) return;
            int target = i - 1;
            editor.cursor_y = segs[target].logical_line;
            int col = std::min(editor.preferred_x, segs[target].char_end);
            if(col < segs[target].char_start) col = segs[target].char_start;
            editor.cursor_x = col;
            scrollToCursor(editor);
            return;
        }
    }
}

static void cursorDown(Editor& editor) {
    rebuildSegmentsIfDirty(editor);
    const auto& segs = editor.last_segments;
    int n = (int)segs.size();
    int found = -1;
    for(int i = 0; i < n; i++) {
        if(segs[i].logical_line == editor.cursor_y &&
           editor.cursor_x >= segs[i].char_start && editor.cursor_x <= segs[i].char_end)
            found = i;
    }
    if(found < 0 || found == n - 1) return;
    int target = found + 1;
    editor.cursor_y = segs[target].logical_line;
    int col = std::min(editor.preferred_x, segs[target].char_end);
    if(col < segs[target].char_start) col = segs[target].char_start;
    editor.cursor_x = col;
    scrollToCursor(editor);
}

static void movePart(Editor& editor, int dir) {
    int n = (int)editor.parts.size();
    int i = editor.active_part;
    int j = i + dir;
    if(j < 0 || j >= n) return;
    std::swap(editor.parts[i], editor.parts[j]);
    editor.active_part = j;
    editor.tabbar.cursor = j;
    editor.segments_dirty = true;
    editor.needs_redraw = true;
}

static void switchPart(Editor& editor, int idx) {
    if(idx < 0 || idx >= (int)editor.parts.size()) return;
    editor.active_part = idx;
    editor.cursor_y = 0;
    editor.cursor_x = 0;
    editor.scroll_offset = 0;
    editor.segments_dirty = true;
    editor.undo_stack.clear();
    editor.redo_stack.clear();
    editor.needs_redraw = true;
}

// Iterative clampTabScroll (fixes recursion)
static void clampTabScroll(Editor& editor) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_y;
    TabBar& tb = editor.tabbar;
    int n = (int)editor.parts.size();
    if(n == 0 || tb.cursor < 0) return;
    tb.cursor = std::max(0, std::min(tb.cursor, n - 1));

    // Scroll left until cursor is reachable
    while(tb.cursor < tb.scroll_offset) tb.scroll_offset--;

    // Scroll right until cursor fits
    while(true) {
        int tab_area_end = max_x - 3;
        int x = 3 + (tb.scroll_offset > 0 ? 2 : 0);
        bool found = false;
        for(int i = tb.scroll_offset; i < n; i++) {
            bool is_renaming = (tb.mode == TabBarMode::RENAMING && tb.rename_idx == i);
            std::string inner = is_renaming ? tb.input_buf : editor.parts[i].name;
            int label_w = (int)inner.size() + 2;
            if(x + label_w > tab_area_end) {
                if(i == tb.cursor) { tb.scroll_offset++; break; }
                break;
            }
            if(i == tb.cursor) { found = true; break; }
            x += label_w + 1;
        }
        if(found || tb.scroll_offset >= n - 1) break;
    }
}

// Global search: finds next match across all parts starting from current position
static void findNext(Editor& editor, bool from_start = false) {
    if(editor.search.query.empty()) return;
    const std::string& q = editor.search.query;
    // lowercase query once
    std::string ql;
    for(char c : q) ql += std::tolower((unsigned char)c);

    int start_part = editor.active_part;
    int start_line = editor.cursor_y;
    int start_col  = from_start ? 0 : editor.cursor_x + 1;

    int total_parts = (int)editor.parts.size();
    for(int pass = 0; pass < 2; pass++) {
        for(int pi = (pass == 0 ? start_part : 0); pi < total_parts; pi++) {
            const auto& plines = editor.parts[pi].lines;
            int li_start = (pass == 0 && pi == start_part) ? start_line : 0;
            for(int li = li_start; li < (int)plines.size(); li++) {
                // case-insensitive: lowercase the line
                std::string ll;
                for(char c : plines[li]) ll += std::tolower((unsigned char)c);
                size_t col_start = (pass == 0 && pi == start_part && li == start_line) ? (size_t)start_col : 0;
                size_t found_col = ll.find(ql, col_start);
                if(found_col != std::string::npos) {
                    if(pi != editor.active_part) switchPart(editor, pi);
                    editor.cursor_y = li;
                    editor.cursor_x = (int)found_col;
                    editor.search.match_part = pi;
                    editor.search.match_line = li;
                    editor.search.match_col  = (int)found_col;
                    scrollToCursor(editor);
                    editor.needs_redraw = true;
                    return;
                }
            }
            start_line = 0; start_col = 0;
        }
    }
}

// Toggle fold — never moves the cursor, just clamps cursor_y to nearest visible line
static void toggleFold(Editor& editor, int line_idx) {
    const std::string& line = editor.lines()[line_idx];
    if(!isHeaderMarker(line, 0)) return;
    auto& folded = editor.parts[editor.active_part].folded_lines;
    if(folded.count(line_idx)) folded.erase(line_idx);
    else folded.insert(line_idx);

    editor.segments_dirty = true;
    rebuildSegmentsIfDirty(editor);

    // Check if cursor_y is still visible
    const auto& segs = editor.last_segments;
    for(auto& s : segs)
        if(s.logical_line == editor.cursor_y) { editor.needs_redraw = true; return; }

    // Cursor hidden: find nearest visible line at or after cursor_y
    int best = -1;
    int best_dist = INT_MAX;
    for(auto& s : segs) {
        if(s.is_continuation) continue;
        int dist = std::abs(s.logical_line - editor.cursor_y);
        if(dist < best_dist) { best_dist = dist; best = s.logical_line; }
    }
    if(best >= 0) {
        editor.cursor_y = best;
        editor.cursor_x = 0;
    }
    // Don't touch scroll_offset — let renderEditor's clampScroll handle it naturally
    editor.needs_redraw = true;
}

// Normalize a string for wikilink matching: lowercase, remove spaces
static std::string normalizeLink(const std::string& s) {
    std::string out;
    for(char c : s) if(c != ' ') out += std::tolower((unsigned char)c);
    return out;
}

// Jump to wikilink: supports "partname" or "partname.header" or "partname.h1.h2"
// Also supports bare "header" that matches within the current part
static void jumpToWikilink(Editor& editor) {
    const std::string& line = editor.lines()[editor.cursor_y];
    int bracket_start = -1;
    for(int i = std::min(editor.cursor_x, (int)line.size() - 1); i >= 0; i--) {
        if(line[i] == '[') { bracket_start = i; break; }
        if(line[i] == ']') break;
    }
    if(bracket_start < 0) return;
    size_t close = line.find(']', bracket_start + 1);
    if(close == std::string::npos) return;
    std::string raw = line.substr(bracket_start + 1, close - bracket_start - 1);

    // Split by '.'
    std::vector<std::string> parts_path;
    {
        std::string seg;
        for(char c : raw) {
            if(c == '.') { if(!seg.empty()) { parts_path.push_back(seg); seg = ""; } }
            else seg += c;
        }
        if(!seg.empty()) parts_path.push_back(seg);
    }
    if(parts_path.empty()) return;

    // Try to match first segment to a part name
    int target_part = editor.active_part;
    int header_start = 0;
    std::string first_norm = normalizeLink(parts_path[0]);
    for(int i = 0; i < (int)editor.parts.size(); i++) {
        if(normalizeLink(editor.parts[i].name) == first_norm) {
            target_part = i;
            header_start = 1;
            break;
        }
    }

    // Navigate to part
    if(target_part != editor.active_part) switchPart(editor, target_part);

    if(header_start >= (int)parts_path.size()) {
        // Just part, go to top
        editor.cursor_y = 0; editor.cursor_x = 0;
        scrollToCursor(editor);
        editor.needs_redraw = true;
        return;
    }

    // Find headers sequentially
    const auto& plines = editor.parts[target_part].lines;
    int search_from = 0;
    int last_found_line = 0;
    for(int hi = header_start; hi < (int)parts_path.size(); hi++) {
        std::string want = normalizeLink(parts_path[hi]);
        for(int li = search_from; li < (int)plines.size(); li++) {
            if(isHeaderMarker(plines[li], 0)) {
                // Extract header text (after "#N ")
                std::string htext = plines[li].substr(3);
                if(normalizeLink(htext) == want) {
                    last_found_line = li;
                    search_from = li + 1;
                    break;
                }
            }
        }
    }
    editor.cursor_y = last_found_line;
    editor.cursor_x = 0;
    scrollToCursor(editor);
    editor.needs_redraw = true;
}

static void handleMouse(Editor& editor, MEVENT& event) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_y;

    if(event.bstate & BUTTON1_RELEASED) return;

    if(event.y == 0) {
        if(event.x == 1) {
            editor.parts.push_back({"New Part", {""}, 0, {}});
            int idx = (int)editor.parts.size() - 1;
            switchPart(editor, idx);
            editor.tabbar.focused = true;
            editor.tabbar.cursor = idx;
            editor.tabbar.mode = TabBarMode::RENAMING;
            editor.tabbar.rename_idx = idx;
            editor.tabbar.input_buf = "";
            editor.needs_redraw = true;
            return;
        }
        if(event.x >= max_x - 2) {
            editor.reading_mode = !editor.reading_mode;
            if(editor.reading_mode) editor.reading_scroll = 0;
            editor.needs_redraw = true;
            return;
        }
        if(event.x == 3 && editor.tabbar.scroll_offset > 0) {
            editor.tabbar.scroll_offset--;
            editor.needs_redraw = true;
            return;
        }
        for(auto& tp : editor.tabbar.tab_positions) {
            if(event.x >= tp.x_start && event.x <= tp.x_end) {
                if(event.bstate & BUTTON1_DOUBLE_CLICKED) {
                    editor.tabbar.focused = true;
                    editor.tabbar.cursor = tp.idx;
                    editor.tabbar.mode = TabBarMode::RENAMING;
                    editor.tabbar.rename_idx = tp.idx;
                    editor.tabbar.input_buf = editor.parts[tp.idx].name;
                } else {
                    switchPart(editor, tp.idx);
                    editor.tabbar.focused = false;
                    editor.tabbar.cursor = tp.idx;
                }
                editor.needs_redraw = true;
                return;
            }
        }
        {
            int n = (int)editor.parts.size();
            int tab_area_end = max_x - 3;
            int x = 3 + (editor.tabbar.scroll_offset > 0 ? 2 : 0);
            bool overflows = false;
            for(int i = editor.tabbar.scroll_offset; i < n; i++) {
                int lw = (int)editor.parts[i].name.size() + 2;
                if(x + lw > tab_area_end) { overflows = true; break; }
                x += lw + 1;
            }
            if(overflows && event.x == x) {
                editor.tabbar.scroll_offset++;
                editor.needs_redraw = true;
            }
        }
        return;
    }

    if(event.y == 1) return;

    if(event.bstate & BUTTON4_PRESSED) {
        int steps = (event.bstate & BUTTON_CTRL) ? 5 : 1;
        for(int i = 0; i < steps; i++) cursorUp(editor);
        editor.needs_redraw = true; return;
    }
    if(event.bstate & BUTTON5_PRESSED) {
        int steps = (event.bstate & BUTTON_CTRL) ? 5 : 1;
        for(int i = 0; i < steps; i++) cursorDown(editor);
        editor.needs_redraw = true; return;
    }
    if(!(event.bstate & BUTTON1_PRESSED)) return;

    editor.tabbar.focused = false;
    int click_row = event.y - 2;
    int seg_idx = editor.scroll_offset + click_row;
    if(seg_idx < 0 || seg_idx >= (int)editor.last_segments.size()) return;
    const SegmentInfo& seg = editor.last_segments[seg_idx];
    const std::string& ln = editor.lines()[seg.logical_line];

    int lnw = lineNumWidth((int)editor.lines().size());
    if(lnw > 0 && event.x == 0 && isHeaderMarker(ln, 0)) {
        toggleFold(editor, seg.logical_line);
        return;
    }

    // Ctrl+Click = wikilink jump
    bool ctrl_held = (event.bstate & BUTTON_CTRL);

    editor.cursor_y = seg.logical_line;
    int effective_col = event.x - (seg.is_continuation ? seg.visual_indent : 0) - (lnw > 0 ? 1 : 0);
    if(effective_col < 0) effective_col = 0;
    int pos = seg.char_start, screen_x = 0;
    while(pos < seg.char_end && screen_x < effective_col) {
        if(pos + 1 < (int)ln.size() && ln[pos] == '*' && ln[pos+1] == '*') { pos += 2; continue; }
        if(isHeaderMarker(ln, pos)) { pos += 3; continue; }
        screen_x++;
        pos += utf8CharLen(ln, pos);
    }
    editor.cursor_x = std::min(pos, seg.char_end);
    editor.preferred_x = editor.cursor_x;

    if(ctrl_held) jumpToWikilink(editor);

    editor.needs_redraw = true;
}

static bool confirmExit(Editor& editor) {
    bool dirty = false;
    if(editor.last_saved_lines.size() != editor.parts.size()) {
        dirty = true;
    } else {
        for(int i = 0; i < (int)editor.parts.size(); i++) {
            if(editor.parts[i].lines != editor.last_saved_lines[i]) { dirty = true; break; }
        }
    }
    if(!dirty) return true;
    setNotification(editor, "Exit? y/s/n");
    renderEditor(editor);
    timeout(-1);
    int ch = getch();
    editor.notification.active = false;
    if(ch == 'y' || ch == 'Y') return true;
    if(ch == 's' || ch == 'S') {
        if(!editor.filepath.empty()) saveFile(editor, editor.filepath);
        setNotification(editor, "Saved!");
        return true;
    }
    return false;
}

static void handleTab(Editor& editor) {
    std::string& line = editor.lines()[editor.cursor_y];
    int indent_start = 0;
    while(indent_start < (int)line.length() && line[indent_start] == ' ') indent_start++;
    if(isBulletLine(line) && editor.cursor_x <= indent_start + 2) {
        line.insert(0, "    "); editor.cursor_x += 4; return;
    }
    line.insert(editor.cursor_x, "    "); editor.cursor_x += 4;
    editor.segments_dirty = true;
}

static void handleEnter(Editor& editor) {
    std::string& cur = editor.lines()[editor.cursor_y];
    std::string remainder = cur.substr(editor.cursor_x);
    cur = cur.substr(0, editor.cursor_x);
    int spaces = 0;
    while(spaces < (int)cur.length() && cur[spaces] == ' ') spaces++;
    std::string new_line(spaces, ' ');
    if(isBulletLine(cur)) new_line += "- ";
    new_line += remainder;
    editor.lines().insert(editor.lines().begin() + editor.cursor_y + 1, new_line);
    editor.cursor_y++;
    editor.cursor_x = (int)new_line.length() - (int)remainder.length();
    editor.segments_dirty = true;
}

static void handleBackspace(Editor& editor) {
    if(editor.cursor_x > 0) {
        std::string& line = editor.lines()[editor.cursor_y];
        int leading = 0;
        while(leading < (int)line.length() && line[leading] == ' ') leading++;
        if(editor.cursor_x <= leading && editor.cursor_x >= 4) {
            line.erase(editor.cursor_x - 4, 4); editor.cursor_x -= 4; return;
        }
        int clen = utf8PrevCharLen(line, editor.cursor_x);
        line.erase(editor.cursor_x - clen, clen);
        editor.cursor_x -= clen;
    } else if(editor.cursor_y > 0 && !isBulletLine(editor.lines()[editor.cursor_y])) {
        editor.cursor_x = (int)editor.lines()[editor.cursor_y - 1].length();
        editor.lines()[editor.cursor_y - 1] += editor.lines()[editor.cursor_y];
        editor.lines().erase(editor.lines().begin() + editor.cursor_y);
        editor.cursor_y--;
    }
    editor.segments_dirty = true;
}

static void handleWordBackspace(Editor& editor) {
    if(editor.cursor_x == 0) {
        if(editor.cursor_y == 0) return;
        editor.cursor_x = (int)editor.lines()[editor.cursor_y - 1].length();
        editor.lines()[editor.cursor_y - 1] += editor.lines()[editor.cursor_y];
        editor.lines().erase(editor.lines().begin() + editor.cursor_y);
        editor.cursor_y--;
        editor.segments_dirty = true;
        return;
    }
    std::string& line = editor.lines()[editor.cursor_y];
    int pos = editor.cursor_x;
    while(pos > 0 && line[pos-1] == ' ') pos -= utf8PrevCharLen(line, pos);
    while(pos > 0 && line[pos-1] != ' ') pos -= utf8PrevCharLen(line, pos);
    line.erase(pos, editor.cursor_x - pos);
    editor.cursor_x = pos;
    editor.segments_dirty = true;
}

static void commitRename(Editor& editor) {
    TabBar& tb = editor.tabbar;
    if(tb.mode != TabBarMode::RENAMING) return;
    if(tb.input_buf.empty()) {
        if((int)editor.parts.size() <= 1) {
            tb.input_buf = editor.parts[tb.rename_idx].name;
            tb.mode = TabBarMode::NORMAL;
            return;
        }
        setNotification(editor, "Delete part? y/n");
        renderEditor(editor);
        timeout(-1);
        int ch = getch();
        editor.notification.active = false;
        if(ch == 'y' || ch == 'Y') {
            int del = tb.rename_idx;
            editor.parts.erase(editor.parts.begin() + del);
            editor.last_saved_lines.clear();
            for(auto& p : editor.parts) editor.last_saved_lines.push_back(p.lines);
            int new_active = std::min(del, (int)editor.parts.size() - 1);
            switchPart(editor, new_active);
            tb.cursor = new_active;
            tb.mode = TabBarMode::NORMAL;
            tb.input_buf = "";
            return;
        }
        tb.input_buf = editor.parts[tb.rename_idx].name;
    }
    editor.parts[tb.rename_idx].name = tb.input_buf;
    tb.mode = TabBarMode::NORMAL;
    tb.input_buf = "";
}

static void finishRename(Editor& editor) {
    commitRename(editor);
    editor.tabbar.focused = false;
}

static void activatePlus(Editor& editor) {
    editor.parts.push_back({"New Part", {""}, 0, {}});
    int idx = (int)editor.parts.size() - 1;
    switchPart(editor, idx);
    editor.tabbar.focused = true;
    editor.tabbar.cursor = idx;
    editor.tabbar.mode = TabBarMode::RENAMING;
    editor.tabbar.rename_idx = idx;
    editor.tabbar.input_buf = "";
}

bool handleInput(Editor& editor, int ch) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_y; (void)max_x;

    if(ch == KEY_RESIZE) {
        while(getch() == KEY_RESIZE);
        ungetch(ERR);
        editor.segments_dirty = true;
        editor.needs_redraw = true;
        return true;
    }

    if(ch == 24 || ch == 3) {
        if(editor.tabbar.mode == TabBarMode::RENAMING) commitRename(editor);
        if(confirmExit(editor)) return false;
        editor.needs_redraw = true;
        return true;
    }

    // Ctrl+E — cycle focus
    if(ch == 5) {
        TabBar& tb = editor.tabbar;
        if(tb.mode == TabBarMode::RENAMING) {
            commitRename(editor);
            int n = (int)editor.parts.size();
            if(tb.cursor + 1 < n) tb.cursor++;
            else tb.cursor = -2;
            clampTabScroll(editor);
        } else if(!tb.focused) {
            tb.focused = true;
            tb.cursor = -1;
        } else if(tb.cursor == -1) {
            tb.cursor = 0;
            clampTabScroll(editor);
        } else if(tb.cursor >= 0 && tb.cursor < (int)editor.parts.size() - 1) {
            tb.cursor++;
            clampTabScroll(editor);
        } else if(tb.cursor == (int)editor.parts.size() - 1) {
            tb.cursor = -2;
        } else {
            tb.focused = false;
        }
        editor.needs_redraw = true;
        return true;
    }

    // reading mode
    if(editor.reading_mode) {
        if(ch == 27) { editor.reading_mode = false; editor.reading_scroll = 0; }
        else if(ch == KEY_UP)   editor.reading_scroll = std::max(0, editor.reading_scroll - 1);
        else if(ch == KEY_DOWN) editor.reading_scroll++;
        else if(ch == KEY_MOUSE) {
            MEVENT event;
            if(getmouse(&event) == OK) {
                if(event.bstate & BUTTON1_RELEASED) { editor.needs_redraw = true; return true; }
                if(event.bstate & BUTTON4_PRESSED)      editor.reading_scroll = std::max(0, editor.reading_scroll - 3);
                else if(event.bstate & BUTTON5_PRESSED) editor.reading_scroll++;
                else if(event.y == 0 && event.x >= max_x - 2) { editor.reading_mode = false; editor.reading_scroll = 0; }
            }
        }
        editor.needs_redraw = true;
        return true;
    }

    if(editor.tabbar.focused) {
        TabBar& tb = editor.tabbar;
        int n = (int)editor.parts.size();

        if(tb.mode == TabBarMode::RENAMING) {
            if(ch == 27) {
                tb.input_buf = editor.parts[tb.rename_idx].name;
                tb.mode = TabBarMode::NORMAL;
                tb.focused = false;
            } else if(ch == '\n' || ch == KEY_ENTER || ch == 13) {
                commitRename(editor);
                tb.focused = false;
            } else if(ch == KEY_LEFT) {
                commitRename(editor); tb.focused = true;
                if(tb.cursor > 0) tb.cursor--;
                else tb.cursor = -1;
                clampTabScroll(editor);
            } else if(ch == KEY_RIGHT) {
                commitRename(editor); tb.focused = true;
                if(tb.cursor + 1 < n) tb.cursor++;
                else tb.cursor = -2;
                clampTabScroll(editor);
            } else if(ch == KEY_BACKSPACE || ch == 127) {
                if(!tb.input_buf.empty()) tb.input_buf.pop_back();
            } else if(ch >= 32 && ch <= 126) {
                tb.input_buf += (char)ch;
            } else if(ch == KEY_MOUSE) {
                MEVENT event;
                if(getmouse(&event) == OK) handleMouse(editor, event);
            }
            editor.needs_redraw = true;
            return true;
        }

        if(ch == 27) { tb.focused = false; editor.needs_redraw = true; return true; }
        if(ch == 19) {
            if(!editor.filepath.empty()) { saveFile(editor, editor.filepath); setNotification(editor, "Saved!"); }
            editor.needs_redraw = true; return true;
        }
        if(ch == KEY_UP && tb.cursor >= 0) {
            editor.parts[tb.cursor].color_idx =
                (editor.parts[tb.cursor].color_idx - 1 + partColorCount()) % partColorCount();
            editor.needs_redraw = true; return true;
        }
        if(ch == KEY_DOWN && tb.cursor >= 0) {
            editor.parts[tb.cursor].color_idx =
                (editor.parts[tb.cursor].color_idx + 1) % partColorCount();
            editor.needs_redraw = true; return true;
        }
        if(ch == KEY_CTRL_LEFT) {
            if(tb.cursor >= 0 && tb.cursor > 0) {
                std::swap(editor.parts[tb.cursor], editor.parts[tb.cursor - 1]);
                if(editor.active_part == tb.cursor) editor.active_part--;
                else if(editor.active_part == tb.cursor - 1) editor.active_part++;
                tb.cursor--;
                clampTabScroll(editor);
            }
            editor.needs_redraw = true; return true;
        }
        if(ch == KEY_CTRL_RIGHT) {
            if(tb.cursor >= 0 && tb.cursor < n - 1) {
                std::swap(editor.parts[tb.cursor], editor.parts[tb.cursor + 1]);
                if(editor.active_part == tb.cursor) editor.active_part++;
                else if(editor.active_part == tb.cursor + 1) editor.active_part--;
                tb.cursor++;
                clampTabScroll(editor);
            }
            editor.needs_redraw = true; return true;
        }
        if(ch == KEY_LEFT) {
            if(tb.cursor == -2) tb.cursor = n - 1;
            else if(tb.cursor > 0) tb.cursor--;
            else if(tb.cursor == 0) tb.cursor = -1;
            clampTabScroll(editor);
            editor.needs_redraw = true; return true;
        }
        if(ch == KEY_RIGHT) {
            if(tb.cursor == -1) tb.cursor = 0;
            else if(tb.cursor >= 0 && tb.cursor < n - 1) tb.cursor++;
            else tb.cursor = -2;
            clampTabScroll(editor);
            editor.needs_redraw = true; return true;
        }
        if(ch == '\n' || ch == KEY_ENTER || ch == 13) {
            if(tb.cursor == -1) {
                activatePlus(editor);
            } else if(tb.cursor == -2) {
                tb.focused = false;
                editor.reading_mode = !editor.reading_mode;
                if(editor.reading_mode) editor.reading_scroll = 0;
            } else {
                switchPart(editor, tb.cursor);
                tb.focused = false;
            }
            editor.needs_redraw = true; return true;
        }
        if(tb.cursor >= 0 && ch >= 32 && ch <= 126) {
            tb.mode = TabBarMode::RENAMING;
            tb.rename_idx = tb.cursor;
            tb.input_buf = "";
            tb.input_buf += (char)ch;
            editor.needs_redraw = true; return true;
        }
        if(ch == KEY_F(2) && tb.cursor >= 0) {
            tb.mode = TabBarMode::RENAMING;
            tb.rename_idx = tb.cursor;
            tb.input_buf = editor.parts[tb.cursor].name;
            editor.needs_redraw = true; return true;
        }
        if(ch == KEY_MOUSE) {
            MEVENT event;
            if(getmouse(&event) == OK) handleMouse(editor, event);
        }
        editor.needs_redraw = true;
        return true;
    }

    // search mode
    if(editor.search.active) {
        if(ch == 27) { editor.search.active = false; editor.needs_redraw = true; }
        else if(ch == '\n' || ch == KEY_ENTER || ch == 13) findNext(editor);
        else if(ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if(!editor.search.query.empty()) { editor.search.query.pop_back(); findNext(editor, true); }
            editor.needs_redraw = true;
        } else if(ch >= 32 && ch <= 126) {
            editor.search.query += (char)ch; findNext(editor, true); editor.needs_redraw = true;
        }
        return true;
    }

    if(ch == KEY_MOUSE) {
        MEVENT event;
        if(getmouse(&event) == OK) handleMouse(editor, event);
        editor.needs_redraw = true;
    }
    else if(ch == 6)  { editor.search = {true, "", -1, -1, -1}; editor.needs_redraw = true; }
    else if(ch == 26) { applyUndo(editor); }
    else if(ch == 25) { applyRedo(editor); }
    else if(ch == 23) {
        int wc = countWords(editor);
        setNotification(editor, "Words: " + std::to_string(wc));
    }
    else if(ch == 2) {
        pushUndo(editor);
        editor.lines()[editor.cursor_y].insert(editor.cursor_x, "****");
        editor.cursor_x += 2;
        editor.segments_dirty = true;
        editor.needs_redraw = true;
    }
    else if(ch == 19) {
        if(!editor.filepath.empty()) { saveFile(editor, editor.filepath); setNotification(editor, "Saved!"); }
    }
    // Ctrl+G = toggle fold (Ctrl+F is search)
    else if(ch == 7) {
        toggleFold(editor, editor.cursor_y);
    }
    else if(ch == '\n' || ch == KEY_ENTER || ch == 13) {
        const std::string& line = editor.lines()[editor.cursor_y];
        bool in_link = false;
        for(int i = 0; i <= editor.cursor_x && i < (int)line.size(); i++) {
            if(line[i] == '[') in_link = true;
            if(line[i] == ']') { if(i >= editor.cursor_x) break; in_link = false; }
        }
        if(in_link) { jumpToWikilink(editor); editor.needs_redraw = true; }
        else { pushUndo(editor); handleEnter(editor); editor.segments_dirty = true; editor.needs_redraw = true; }
    }
    else if(ch == '\t') { pushUndo(editor); handleTab(editor); editor.segments_dirty = true; editor.needs_redraw = true; }
    else if(ch == KEY_UP) { cursorUp(editor); editor.needs_redraw = true; }
    else if(ch == KEY_DOWN) { cursorDown(editor); editor.needs_redraw = true; }
    // Ctrl+Up/Down: scroll 5 segments
    else if(ch == 566) { for(int i = 0; i < 5; i++) cursorUp(editor); editor.needs_redraw = true; }
    else if(ch == 525) { for(int i = 0; i < 5; i++) cursorDown(editor); editor.needs_redraw = true; }
    else if(ch == KEY_CTRL_LEFT)  { movePart(editor, -1); }
    else if(ch == KEY_CTRL_RIGHT) { movePart(editor, 1); }
    else if(ch == KEY_LEFT) {
        const std::string& line = editor.lines()[editor.cursor_y];
        if(editor.cursor_x >= 2 && line[editor.cursor_x-2] == '*' && line[editor.cursor_x-1] == '*') editor.cursor_x -= 2;
        else if(editor.cursor_x > 0) editor.cursor_x -= utf8PrevCharLen(line, editor.cursor_x);
        editor.preferred_x = editor.cursor_x;
        editor.needs_redraw = true;
    }
    else if(ch == KEY_RIGHT) {
        const std::string& line = editor.lines()[editor.cursor_y];
        if(editor.cursor_x + 1 < (int)line.length() && line[editor.cursor_x] == '*' && line[editor.cursor_x+1] == '*') editor.cursor_x += 2;
        else if(editor.cursor_x < (int)line.length()) editor.cursor_x += utf8CharLen(line, editor.cursor_x);
        editor.preferred_x = editor.cursor_x;
        editor.needs_redraw = true;
    }
    else if(ch == 8)  { pushUndo(editor); handleWordBackspace(editor); editor.segments_dirty = true; editor.needs_redraw = true; }
    else if(ch == KEY_BACKSPACE || ch == 127) { pushUndo(editor); handleBackspace(editor); editor.segments_dirty = true; editor.needs_redraw = true; }
    else if(ch == '-' && editor.cursor_x == 0) {
        pushUndo(editor);
        if(!isBulletLine(editor.lines()[editor.cursor_y])) { editor.lines()[editor.cursor_y].insert(0, "- "); editor.cursor_x = 2; }
        else { editor.lines()[editor.cursor_y].insert(editor.cursor_x, 1, '-'); editor.cursor_x++; }
        editor.segments_dirty = true;
        editor.needs_redraw = true;
    }
    else if(ch == '-') {
        std::string& line = editor.lines()[editor.cursor_y];
        int spaces = 0;
        while(spaces < (int)line.length() && line[spaces] == ' ') spaces++;
        pushUndo(editor);
        if(editor.cursor_x <= spaces && !isBulletLine(line)) { line.insert(editor.cursor_x, "- "); editor.cursor_x += 2; }
        else { line.insert(editor.cursor_x, 1, '-'); editor.cursor_x++; }
        editor.segments_dirty = true;
        editor.needs_redraw = true;
    }
    else if(ch >= 32 && ch <= 126) {
        pushUndoIfWordBoundary(editor, (char)ch);
        editor.lines()[editor.cursor_y].insert(editor.cursor_x, 1, (char)ch);
        editor.cursor_x++;
        editor.preferred_x = editor.cursor_x;
        editor.segments_dirty = true;
        editor.needs_redraw = true;
    }
    return true;
}

