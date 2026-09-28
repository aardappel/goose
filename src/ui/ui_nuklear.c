/* Nuklear's implementation, compiled once, with the configuration every
   file of the layer includes it with (ui_internal.h). Built without
   warnings: third-party code is not ours to act on. */

#define NK_IMPLEMENTATION
#include "ui_internal.h"

#include <limits.h>

/* Here, where Nuklear's own table is in scope. */
int64_t gs_ui_default_color_table(gs_ui_color_slice out) {
    for (int64_t i = 0; i < NK_COLOR_COUNT && i < out.len; i++) {
        struct nk_color c = nk_default_color_style[i];
        gs_ui_color g = { c.r, c.g, c.b, c.a };
        out.data[i] = g;
    }
    return NK_COLOR_COUNT;
}

/* Here, where nk_textedit_makeundo_insert is in scope. */
bool ui_textedit_paste(struct nk_text_edit *e, const char *text, int len) {
    if (e->mode == NK_TEXT_EDIT_MODE_VIEW || len <= 0) return false;
    nk_textedit_clamp(e);
    nk_textedit_delete_selection(e);
    const struct nk_buffer *b = &e->string.buffer;
    int room = b->type == NK_BUFFER_FIXED ? (int)b->memory.size - 1 - (int)b->allocated : INT_MAX;
    char *keep = (char *)malloc((size_t)len);
    if (!keep) return false;
    int n = 0, chars = 0, g;
    nk_rune r;
    for (int at = 0; at < len && (g = nk_utf_decode(text + at, &r, len - at)) > 0; at += g) {
        if (r == 127 || (r == '\n' && e->single_line) || (e->filter && !e->filter(e, r)))
            continue;
        if (g > room - n) break;
        memcpy(keep + n, text + at, (size_t)g);
        n += g;
        chars++;
    }
    bool pasted = n > 0 && nk_str_insert_at_rune(&e->string, e->cursor, keep, n);
    if (pasted) {
        nk_textedit_makeundo_insert(e, e->cursor, chars);
        e->cursor += chars;
        e->has_preferred_x = 0;
    }
    free(keep);
    return pasted;
}

/* Here, where Nuklear's property states are in scope. Typed text reaches
   a window only while it takes input: it is not hidden, closed or
   minimized, nor read only, which Nuklear makes the windows behind the
   one in front (background ones aside) and a window with a popup open.
   There the text goes to the property being typed into, or else to the
   text field that has the keyboard: edit.active, which a property sets
   too while it is dragged, if the window had a text field in that place
   last frame. */
static bool ui_takes_text(const struct nk_window *w) {
    if (w->flags & (NK_WINDOW_HIDDEN | NK_WINDOW_CLOSED | NK_WINDOW_MINIMIZED | NK_WINDOW_ROM))
        return false;
    if (w->property.active) return w->property.state == NK_PROPERTY_EDIT;
    return w->edit.active && w->edit.name < w->edit.old;
}

uint8_t gs_ui_wants_keyboard(gs_ui_context c) {
    UI_CTX(c, "ui::wants_keyboard", 0);
    for (const struct nk_window *w = ctx->begin; w; w = w->next) {
        if (!ui_window_current(u, w)) continue;
        if (ui_takes_text(w)) return 1;
        if (w->popup.active && w->popup.win && !(w->flags & NK_WINDOW_HIDDEN) &&
            ui_takes_text(w->popup.win))
            return 1;
    }
    return 0;
}

/* Every constant of ui_api.h has the value of Nuklear's of the same name,
   but the few that are the layer's own, which stand in for themselves (the
   end of the file, where Nuklear's NK_FILTER_FLOAT is no longer used). */
#define NK_FILTER_DEFAULT GS_UI_FILTER_DEFAULT
#define NK_FILTER_ASCII GS_UI_FILTER_ASCII
#define NK_FILTER_FLOAT GS_UI_FILTER_FLOAT
#define NK_FILTER_DECIMAL GS_UI_FILTER_DECIMAL
#define NK_FILTER_HEX GS_UI_FILTER_HEX
#define NK_FILTER_OCT GS_UI_FILTER_OCT
#define NK_FILTER_BINARY GS_UI_FILTER_BINARY
#define NK_RANGE_DEFAULT GS_UI_RANGE_DEFAULT
#define NK_RANGE_CHINESE GS_UI_RANGE_CHINESE
#define NK_RANGE_CYRILLIC GS_UI_RANGE_CYRILLIC
#define NK_RANGE_KOREAN GS_UI_RANGE_KOREAN
#define UI_SAME_AS_NUKLEAR(type, name, value) _Static_assert(GS_UI_##name == NK_##name, #name);
GS_UI_CONSTANTS(UI_SAME_AS_NUKLEAR)
#undef UI_SAME_AS_NUKLEAR
