#include "render.h"
#include "util.h"
#include <ncurses.h>
#include <vector>
#include <string>
#include <ctime>
#include <wchar.h>
#include <locale.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

static int utf8ScreenWidth(const std::string& s, int byte_pos) {
    wchar_t wc;
    int len = utf8CharLen(s, byte_pos);
    char buf[5] = {};
    for(int i = 0; i < len && byte_pos + i < (int)s.size(); i++) buf[i] = s[byte_pos + i];
    if(mbtowc(&wc, buf, len) > 0) { int w = wcwidth(wc); return w > 0 ? w : 1; }
    return 1;
}

static int wikilinkAt(const std::string& line, int i, int& end) {
    if(line[i] != '[') return -1;
    size_t close = line.find(']', i + 1);
    if(close == std::string::npos) return -1;
    end = (int)close + 1;
    return i;
}

static int visibleScreenWidth(const std::string& line, int from, int to, bool count_markers = false) {
    int w = 0, i = from;
    while(i < to) {
        if(i + 1 < (int)line.size() && line[i] == '*' && line[i+1] == '*') {
            if(count_markers) w += 2;
            i += 2; continue;
        }
        if(isHeaderMarker(line, i)) {
            if(count_markers) w += 3;
            i += 3; continue;
        }
        if(line[i] == '[') {
            int end_pos;
            if(wikilinkAt(line, i, end_pos) >= 0 && end_pos <= to) {
                int inner_start = i + 1;
                int inner_end   = end_pos - 1;
                int display_start = inner_start;
                for(int di = inner_start; di < inner_end; di++)
                    if(line[di] == '.') display_start = di + 1;
                w += visibleScreenWidth(line, display_start, inner_end, count_markers);
                i = end_pos; continue;
            }
        }
        w += utf8ScreenWidth(line, i);
        i += utf8CharLen(line, i);
    }
    return w;
}

static void buildLineSegments(const std::string& line, int li, int max_x, int cursor_line,
                               std::vector<SegmentInfo>& segs) {
    bool reveal = (cursor_line == li);
    if(line.empty()) { segs.push_back({li, 0, 0, false, 0}); return; }
    int leading = 0;
    while(leading < (int)line.length() && line[leading] == ' ') leading++;
    int visual_indent = leading;
    if(leading + 1 < (int)line.length() && line[leading] == '-' && line[leading+1] == ' ')
        visual_indent = leading + 2;
    bool first = true;
    int seg_start = 0, screen_w = 0, i = 0;
    while(i <= (int)line.length()) {
        if(i == (int)line.length()) {
            segs.push_back({li, seg_start, i, !first, first ? 0 : visual_indent});
            break;
        }
        if(i + 1 < (int)line.length() && line[i] == '*' && line[i+1] == '*') {
            if(reveal) screen_w += 2;
            i += 2; continue;
        }
        if(isHeaderMarker(line, i)) {
            if(reveal) screen_w += 3;
            i += 3; continue;
        }
        if(line[i] == '[') {
            int end_pos;
            if(wikilinkAt(line, i, end_pos) >= 0) {
                int inner_start = i + 1;
                int inner_end   = end_pos - 1;
                int inner_w;
                if(reveal) {
                    inner_w = 2 + visibleScreenWidth(line, inner_start, inner_end, true);
                } else {
                    int display_start = inner_start;
                    for(int di = inner_start; di < inner_end; di++)
                        if(line[di] == '.') display_start = di + 1;
                    inner_w = visibleScreenWidth(line, display_start, inner_end);
                }
                int effective_max = first ? max_x : max_x - visual_indent;
                if(screen_w + inner_w > effective_max) {
                    int break_pos = i;
                    while(break_pos > seg_start && line[break_pos] != ' ') break_pos--;
                    if(break_pos == seg_start) break_pos = i;
                    else break_pos++;
                    segs.push_back({li, seg_start, break_pos, !first, first ? 0 : visual_indent});
                    first = false;
                    seg_start = break_pos;
                    if(seg_start < (int)line.length() && line[seg_start] == ' ') seg_start++;
                    screen_w = 0;
                    i = seg_start;
                    continue;
                }
                screen_w += inner_w;
                i = end_pos;
                continue;
            }
        }
        int clen = utf8CharLen(line, i);
        int cw = utf8ScreenWidth(line, i);
        int effective_max = first ? max_x : max_x - visual_indent;
        if(screen_w + cw > effective_max) {
            int break_pos = i;
            while(break_pos > seg_start && line[break_pos] != ' ') break_pos--;
            if(break_pos == seg_start) break_pos = i;
            else break_pos++;
            segs.push_back({li, seg_start, break_pos, !first, first ? 0 : visual_indent});
            first = false;
            seg_start = break_pos;
            if(seg_start < (int)line.length() && line[seg_start] == ' ') seg_start++;
            screen_w = 0;
            i = seg_start;
        } else { screen_w += cw; i += clen; }
    }
}

std::vector<SegmentInfo> buildSegments(const std::vector<std::string>& lines, int max_x,
                                        const std::set<int>* folded, int cursor_line) {
    std::vector<SegmentInfo> segs;
    int n = (int)lines.size();
    int li = 0;
    while(li < n) {
        const std::string& line = lines[li];
        if(folded && !folded->empty() && folded->count(li)) {
            segs.push_back({li, 0, (int)line.size(), false, 0});
            int fold_level = (isHeaderMarker(line, 0)) ? (line[1] - '0') : 0;
            li++;
            while(li < n) {
                const std::string& sl = lines[li];
                if(isHeaderMarker(sl, 0) && (sl[1] - '0') <= fold_level) break;
                li++;
            }
            continue;
        }
        buildLineSegments(line, li, max_x, cursor_line, segs);
        li++;
    }
    return segs;
}

static int headerColorPair(char digit) {
    if(digit == '1') return CP_H1;
    if(digit == '2') return CP_H2;
    return CP_H3;
}

static void renderSegment(int y, int start_x, const std::string& line, int char_start, int char_end,
                           int cursor_pos, bool is_continuation,
                           int visual_indent, const std::string& search_query,
                           bool is_folded_header, bool cursor_on_line) {
    int x = start_x;
    if(is_continuation && visual_indent > 0) {
        static char spaces[256];
        int n = std::min(visual_indent, 255);
        memset(spaces, ' ', n);
        mvaddnstr(y, x, spaces, n);
        x += n;
    }

    if(!is_continuation && isHeaderMarker(line, 0)) {
        attron(COLOR_PAIR(CP_SUBTLE));
        mvaddch(y, start_x == 0 ? 0 : start_x - 1, is_folded_header ? '>' : ' ');
        attroff(COLOR_PAIR(CP_SUBTLE));
    }

    std::string line_lower;
    std::string sq_lower;
    int sq_len = (int)search_query.size();
    if(sq_len > 0) {
        for(char c : line) line_lower += std::tolower((unsigned char)c);
        for(char c : search_query) sq_lower += std::tolower((unsigned char)c);
    }

    bool in_bold = false;
    int header_color = 0;
    int cur_attr = A_NORMAL;

    if(char_start > 0) {
        int pi = 0;
        while(pi < char_start) {
            if(pi + 1 < (int)line.size() && line[pi] == '*' && line[pi+1] == '*') {
                in_bold = !in_bold; pi += 2; continue;
            }
            if(isHeaderMarker(line, pi)) {
                header_color = (header_color == 0) ? headerColorPair(line[pi+1]) : 0;
                pi += 3; continue;
            }
            pi += utf8CharLen(line, pi);
        }
    }

    int i = char_start;

    while(i < char_end) {
        if(i + 1 < (int)line.size() && line[i] == '*' && line[i+1] == '*') {
            in_bold = !in_bold;
            if(cursor_on_line) {
                int a = COLOR_PAIR(CP_ACCENT) | A_BOLD;
                if(a != cur_attr) { attrset(a); cur_attr = a; }
                mvaddch(y, x++, '*'); mvaddch(y, x++, '*');
            }
            i += 2; continue;
        }
        if(isHeaderMarker(line, i)) {
            int hcp = headerColorPair(line[i+1]);
            if(!cursor_on_line) {
                header_color = (header_color == 0) ? hcp : 0;
            } else {
                header_color = hcp;
                int a = COLOR_PAIR(hcp) | A_BOLD;
                if(a != cur_attr) { attrset(a); cur_attr = a; }
                mvaddch(y, x++, '#'); mvaddch(y, x++, line[i+1]); mvaddch(y, x++, ' ');
            }
            i += 3; continue;
        }
        if(line[i] == '[') {
            int end_pos;
            if(wikilinkAt(line, i, end_pos) >= 0 && end_pos <= char_end) {
                int inner_start = i + 1;
                int inner_end   = end_pos - 1;
                int a = COLOR_PAIR(CP_ACCENT) | A_UNDERLINE;
                if(cursor_on_line) {
                    if(a != cur_attr) { attrset(a); cur_attr = a; }
                    mvaddch(y, x++, '[');
                    mvaddnstr(y, x, line.c_str() + inner_start, inner_end - inner_start);
                    x += visibleScreenWidth(line, inner_start, inner_end, true);
                    mvaddch(y, x++, ']');
                } else {
                    int display_start = inner_start;
                    for(int di = inner_start; di < inner_end; di++)
                        if(line[di] == '.') display_start = di + 1;
                    if(a != cur_attr) { attrset(a); cur_attr = a; }
                    mvaddnstr(y, x, line.c_str() + display_start, inner_end - display_start);
                    x += visibleScreenWidth(line, display_start, inner_end);
                }
                i = end_pos;
                continue;
            }
        }
        if(sq_len > 0 && i + sq_len <= char_end &&
           memcmp(line_lower.c_str() + i, sq_lower.c_str(), sq_len) == 0) {
            int a = COLOR_PAIR(CP_SEARCH_HL) | A_BOLD;
            if(a != cur_attr) { attrset(a); cur_attr = a; }
            int sw = visibleScreenWidth(line, i, i + sq_len);
            mvaddnstr(y, x, line.c_str() + i, sq_len);
            x += sw; i += sq_len;
            continue;
        }
        int new_attr;
        if(header_color) new_attr = COLOR_PAIR(header_color) | A_BOLD;
        else new_attr = in_bold ? (COLOR_PAIR(CP_ACCENT) | A_BOLD) : A_NORMAL;
        if(new_attr != cur_attr) { attrset(new_attr); cur_attr = new_attr; }
        int run_start = i;
        while(i < char_end) {
            unsigned char c = line[i];
            if(c >= 0x80) break;
            if(i + 1 < (int)line.size() && line[i] == '*' && line[i+1] == '*') break;
            if(isHeaderMarker(line, i)) break;
            if(line[i] == '[') break;
            if(sq_len > 0 && i + sq_len <= char_end &&
               memcmp(line_lower.c_str() + i, sq_lower.c_str(), sq_len) == 0) break;
            i++;
        }
        if(i > run_start) {
            mvaddnstr(y, x, line.c_str() + run_start, i - run_start);
            x += i - run_start;
        } else {
            int clen = utf8CharLen(line, i), cw = utf8ScreenWidth(line, i);
            char buf[5] = {};
            for(int k = 0; k < clen; k++) buf[k] = line[i+k];
            mvaddstr(y, x, buf); x += cw; i += clen;
        }
    }
    if(cur_attr != A_NORMAL) attrset(A_NORMAL);
}

static void renderBox(int y, int x, int w, int h, int max_y, int max_x) {
    if(y < 0 || x < 0 || y + h > max_y || x + w > max_x) return;
    attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
    mvaddstr(y, x, "╭"); mvaddstr(y, x+w-1, "╮");
    mvaddstr(y+h-1, x, "╰"); mvaddstr(y+h-1, x+w-1, "╯");
    for(int i = 1; i < w-1; i++) { mvaddstr(y, x+i, "─"); mvaddstr(y+h-1, x+i, "─"); }
    for(int i = 1; i < h-1; i++) {
        mvaddstr(y+i, x, "│"); mvaddstr(y+i, x+w-1, "│");
        for(int j = 1; j < w-1; j++) mvaddch(y+i, x+j, ' ');
    }
    attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
}

static void renderNotification(const Notification& notif, int max_y, int max_x) {
    if(!notif.active) return;
    const std::string& msg = notif.message;
    int w = (int)msg.size() + 4, x = max_x - w - 1, y = max_y - 4;
    if(x < 0 || y < 0 || y + 3 > max_y || x + w > max_x) return;
    renderBox(y, x, w, 3, max_y, max_x);
    attron(A_BOLD); mvaddstr(y+1, x+2, msg.c_str()); attroff(A_BOLD);
}

static void renderSearch(const SearchState& search, int max_y, int max_x) {
    if(!search.active) return;
    const std::string prefix = "Search: ";
    const std::string& q = search.query;
    int min_w = (int)prefix.size() + 4;
    int w = std::max(min_w, (int)(prefix.size() + q.size()) + 4);
    w = std::min(w, max_x - 4);
    int x = (max_x - w) / 2, y = max_y - 4;
    if(y < 0 || x < 0 || y + 3 > max_y || x + w > max_x) return;
    renderBox(y, x, w, 3, max_y, max_x);
    attron(A_BOLD);
    std::string display = prefix + q;
    if((int)display.size() > w - 4) display = display.substr(display.size() - (w - 4));
    mvaddstr(y+1, x+2, display.c_str());
    attroff(A_BOLD);
    move(y+1, x+2+(int)std::min(prefix.size()+q.size(), (size_t)(w-4)));
}

static void renderTabBar(Editor& editor, int max_x) {
    move(0, 0); clrtoeol();
    move(1, 0); clrtoeol();

    TabBar& tb = editor.tabbar;
    const auto& parts = editor.parts;
    int n = (int)parts.size();

    bool plus_focused = tb.focused && tb.cursor == -1;
    bool at_focused   = tb.focused && tb.cursor == -2;

    if(plus_focused) attron(COLOR_PAIR(CP_READING) | A_BOLD);
    else attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
    mvaddstr(0, 1, "+");
    if(plus_focused) attroff(COLOR_PAIR(CP_READING) | A_BOLD);
    else attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);

    int tab_area_end = max_x - 3;
    int x = 3;
    if(tb.scroll_offset > 0) {
        attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
        mvaddch(0, x, '<');
        attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
        x += 2;
    }

    tb.tab_positions.clear();
    bool has_right = false;

    for(int i = tb.scroll_offset; i < n; i++) {
        bool is_active   = (i == editor.active_part);
        bool is_focused  = (tb.focused && tb.cursor == i);
        bool is_renaming = (tb.mode == TabBarMode::RENAMING && tb.rename_idx == i);

        std::string inner = is_renaming ? tb.input_buf : parts[i].name;
        std::string label = is_focused ? ("<" + inner + ">") : (" " + inner + " ");
        int label_w = (int)label.size();

        if(x + label_w > tab_area_end) { has_right = true; break; }
        tb.tab_positions.push_back({i, x, x + label_w - 1});

        int cp = partColorPair(parts[i].color_idx);
        attr_t attr = COLOR_PAIR(cp);
        if(is_active || is_focused) attr |= A_BOLD | A_UNDERLINE;

        attron(attr);
        mvaddstr(0, x, label.c_str());
        attroff(attr);

        x += label_w + 1;
    }

    if(has_right) {
        attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
        mvaddch(0, x, '>');
        attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
    }

    if(editor.reading_mode || at_focused) attron(COLOR_PAIR(CP_READING) | A_BOLD);
    else attron(COLOR_PAIR(CP_DIM));
    mvaddstr(0, max_x - 2, "@");
    if(editor.reading_mode || at_focused) attroff(COLOR_PAIR(CP_READING) | A_BOLD);
    else attroff(COLOR_PAIR(CP_DIM));

    attron(COLOR_PAIR(CP_SUBTLE));
    mvhline(1, 0, ACS_HLINE, max_x);
    attroff(COLOR_PAIR(CP_SUBTLE));
}

static void renderReadingMode(const Editor& editor, int max_y, int max_x) {
    move(0, 0); clrtoeol();
    attron(COLOR_PAIR(CP_READING) | A_BOLD);
    mvaddstr(0, max_x - 2, "@");
    attroff(COLOR_PAIR(CP_READING) | A_BOLD);

    int text_w = max_x;
    int row = 1;

    struct ReadLine { int part; const std::string* text; bool is_header; bool is_blank;
                      int seg_cs; int seg_ce; bool seg_cont; int seg_vi; };
    std::vector<ReadLine> lines_to_render;

    for(int p = 0; p < (int)editor.parts.size(); p++) {
        lines_to_render.push_back({p, nullptr, true, false, 0, 0, false, 0});
        lines_to_render.push_back({p, nullptr, false, true, 0, 0, false, 0});
        auto segs = buildSegments(editor.parts[p].lines, text_w, &editor.parts[p].folded_lines);
        for(auto& seg : segs)
            lines_to_render.push_back({p, &editor.parts[p].lines[seg.logical_line], false, false,
                                       seg.char_start, seg.char_end, seg.is_continuation, seg.visual_indent});
        lines_to_render.push_back({p, nullptr, false, true, 0, 0, false, 0});
    }

    int total = (int)lines_to_render.size();
    int scroll = std::max(0, std::min(editor.reading_scroll, std::max(0, total - (max_y - 1))));

    for(int i = scroll; i < total && row < max_y; i++, row++) {
        move(row, 0); clrtoeol();
        auto& rl = lines_to_render[i];
        if(rl.is_blank) continue;
        if(rl.is_header) {
            std::string header = editor.parts[rl.part].name;
            for(auto& c : header) c = std::toupper(c);
            int cp = partColorPair(editor.parts[rl.part].color_idx);
            attron(COLOR_PAIR(cp) | A_BOLD);
            mvaddstr(row, 0, header.c_str());
            attroff(COLOR_PAIR(cp) | A_BOLD);
        } else {
            renderSegment(row, 0, *rl.text, rl.seg_cs, rl.seg_ce, -1, rl.seg_cont, rl.seg_vi, "", false, false);
        }
    }
    while(row < max_y) { move(row++, 0); clrtoeol(); }
}

void initRender() {
    initscr();
    start_color();
    use_default_colors();
    init_pair(CP_ACCENT,    COLOR_BLUE,    -1);
    init_pair(CP_SEARCH_HL, COLOR_BLACK,   COLOR_MAGENTA);
    init_pair(CP_DIM,       COLOR_WHITE,   -1);
    init_pair(CP_READING,   210,           -1);
    init_pair(CP_H1,        214,           -1);
    init_pair(CP_H2,        COLOR_GREEN,   -1);
    init_pair(CP_H3,        135,           -1);
    init_pair(CP_SUBTLE,    8,             -1);
    init_pair(CP_PART_BASE+0, COLOR_BLUE,    -1);
    init_pair(CP_PART_BASE+1, 135,           -1);
    init_pair(CP_PART_BASE+2, 210,           -1);
    init_pair(CP_PART_BASE+3, 214,           -1);
    init_pair(CP_PART_BASE+4, COLOR_YELLOW,  -1);
    init_pair(CP_PART_BASE+5, COLOR_GREEN,   -1);
    init_pair(CP_PART_BASE+6, COLOR_CYAN,    -1);
    init_pair(CP_PART_BASE+7, COLOR_WHITE,   -1);
    raw();
    noecho();
    keypad(stdscr, TRUE);
    leaveok(stdscr, TRUE);
    define_key("\033[1;5D", KEY_CTRL_LEFT);
    define_key("\033[1;5C", KEY_CTRL_RIGHT);
    define_key("\033[1;5A", 566);
    define_key("\033[1;5B", 525);
    timeout(-1);
    curs_set(1);
    printf("\033[6 q");
    fflush(stdout);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    mouseinterval(0);
}

static void clampScroll(Editor& editor, int total_segs, int content_rows) {
    int max_scroll = std::max(0, total_segs - 1);
    editor.scroll_offset = std::max(0, std::min(editor.scroll_offset, max_scroll));
}

static int lineNumWidth(int total_lines) {
    int w = 1;
    while(total_lines >= 10) { total_lines /= 10; w++; }
    return w;
}

static int computeCursorScreenX(const std::string& line, int seg_start, int cursor_x, bool cursor_on_line) {
    int x = 0;
    int i = seg_start;
    while(i < cursor_x) {
        if(i + 1 < (int)line.size() && line[i] == '*' && line[i+1] == '*') {
            if(cursor_on_line) {
                if(i < cursor_x) { x += 1; i++; }
                if(i < cursor_x) { x += 1; i++; }
            } else {
                i += 2;
            }
            continue;
        }
        if(isHeaderMarker(line, i)) {
            if(cursor_on_line) {
                if(i < cursor_x) { x += 1; i++; }
                if(i < cursor_x) { x += 1; i++; }
                if(i < cursor_x) { x += 1; i++; }
            } else {
                i += 3;
            }
            continue;
        }
        if(line[i] == '[') {
            int end_pos;
            if(wikilinkAt(line, i, end_pos) >= 0) {
                if(cursor_on_line) {
                    if(i < cursor_x) { x += 1; i++; }
                    while(i < end_pos - 1 && i < cursor_x) {
                        x += utf8ScreenWidth(line, i);
                        i += utf8CharLen(line, i);
                    }
                    if(i >= cursor_x) return x;
                    if(i < cursor_x) { x += 1; i++; }
                    if(i >= end_pos) continue;
                } else {
                    int inner_start = i + 1;
                    int inner_end   = end_pos - 1;
                    int display_start = inner_start;
                    for(int di = inner_start; di < inner_end; di++)
                        if(line[di] == '.') display_start = di + 1;
                    if(cursor_x >= end_pos) {
                        x += visibleScreenWidth(line, display_start, inner_end);
                        i = end_pos;
                    } else {
                        int disp_pos = std::max(display_start, std::min(cursor_x, inner_end));
                        x += visibleScreenWidth(line, display_start, disp_pos);
                        return x;
                    }
                    continue;
                }
                continue;
            }
        }
        x += utf8ScreenWidth(line, i);
        i += utf8CharLen(line, i);
    }
    return x;
}

void renderEditor(Editor& editor) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    if(max_y < 3 || max_x < 4) return;

    curs_set(0);

    editor.cursor_y = std::max(0, std::min(editor.cursor_y, (int)editor.lines().size() - 1));
    editor.cursor_x = std::max(0, std::min(editor.cursor_x, (int)editor.lines()[editor.cursor_y].length()));

    if(editor.reading_mode) {
        for(int r = 0; r < max_y; r++) { move(r, 0); clrtoeol(); }
        renderReadingMode(editor, max_y, max_x);
        refresh();
        return;
    }

    renderTabBar(editor, max_x);

    int content_y_start = 2;
    int content_rows = max_y - content_y_start;
    if(content_rows < 1) return;

    int lnw = lineNumWidth((int)editor.lines().size());
    int text_w = max_x - lnw - 1;
    if(text_w < 4) { lnw = 0; text_w = max_x; }

    if(editor.segments_dirty) {
        editor.last_segments = buildSegments(editor.lines(), text_w,
            &editor.parts[editor.active_part].folded_lines, editor.cursor_y);
        editor.segments_dirty = false;
    }
    const auto& segs = editor.last_segments;

    int max_scroll = std::max(0, (int)segs.size() - 1);
    editor.scroll_offset = std::max(0, std::min(editor.scroll_offset, max_scroll));

    int cursor_screen_row = -1;
    int cursor_screen_col = 0;

    for(int r = 0; r < content_rows; r++) {
        int seg_idx = editor.scroll_offset + r;
        if(seg_idx >= (int)segs.size()) break;
        const auto& seg = segs[seg_idx];
        if(seg.logical_line == editor.cursor_y &&
           editor.cursor_x >= seg.char_start && editor.cursor_x <= seg.char_end) {
            cursor_screen_row = content_y_start + r;
            int col_off = lnw > 0 ? 1 : 0;
            int indent   = seg.is_continuation ? seg.visual_indent : 0;
            cursor_screen_col = col_off + indent +
                computeCursorScreenX(editor.lines()[editor.cursor_y], seg.char_start, editor.cursor_x, true);
            break;
        }
    }

    for(int r = content_y_start; r < max_y; r++) { move(r, 0); clrtoeol(); }

    const std::string& sq = editor.search.active ? editor.search.query : std::string();

    for(int row = 0; row < content_rows; row++) {
        int seg_idx = editor.scroll_offset + row;
        if(seg_idx >= (int)segs.size()) break;
        const auto& seg = segs[seg_idx];
        bool cursor_on_line = (seg.logical_line == editor.cursor_y);
        int screen_row = content_y_start + row;
        const std::string& line = editor.lines()[seg.logical_line];
        bool is_folded = editor.parts[editor.active_part].folded_lines.count(seg.logical_line) > 0;

        renderSegment(screen_row, lnw > 0 ? 1 : 0, line, seg.char_start, seg.char_end,
                      editor.cursor_x, seg.is_continuation, seg.visual_indent, sq, is_folded,
                      cursor_on_line);

        if(lnw > 0 && !seg.is_continuation) {
            attron(COLOR_PAIR(CP_SUBTLE));
            char lnbuf[16];
            snprintf(lnbuf, sizeof(lnbuf), "%*d", lnw, seg.logical_line + 1);
            mvaddstr(screen_row, max_x - lnw, lnbuf);
            attroff(COLOR_PAIR(CP_SUBTLE));
        }
    }

    if(editor.search.active) {
        renderSearch(editor.search, max_y, max_x);
        refresh();
    } else {
        if(editor.notification.active)
            renderNotification(editor.notification, max_y, max_x);
        if(cursor_screen_row >= 0) {
            move(cursor_screen_row, cursor_screen_col);
            leaveok(stdscr, FALSE);
            curs_set(1);
            refresh();
            leaveok(stdscr, TRUE);
        } else {
            refresh();
        }
    }
}
