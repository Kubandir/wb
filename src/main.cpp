#include "editor.h"
#include "render.h"
#include "input.h"
#include "file_ops.h"
#include <ncurses.h>
#include <ctime>
#include <locale.h>

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");
    Editor editor;
    initEditor(editor);
    initRender();

    if(argc > 1) {
        loadFile(editor, argv[1]);
        setNotification(editor, "Hello!");
    }

    time_t last_autosave = time(nullptr);

    while(true) {
        time_t now = time(nullptr);
        if(!editor.filepath.empty() && now - last_autosave >= 60) {
            saveFile(editor, editor.filepath);
            setNotification(editor, "Saved!");
            last_autosave = now;
        }

        if(editor.notification.active) {
            int ms_left = (int)(3000 - (now - editor.notification.shown_at) * 1000);
            if(ms_left <= 0) {
                editor.notification.active = false;
                editor.needs_redraw = true;
                timeout(-1);
            } else {
                timeout(ms_left);
            }
        }

        if(editor.needs_redraw) {
            renderEditor(editor);
            editor.needs_redraw = false;
        }

        int ch = getch();
        now = time(nullptr);  // refresh after blocking
        if(ch == ERR) {
            editor.notification.active = false;
            editor.needs_redraw = true;
            timeout(-1);
            continue;
        }
        if(!handleInput(editor, ch)) break;
    }

    endwin();
    return 0;
}
