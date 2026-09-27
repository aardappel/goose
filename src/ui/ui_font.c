/* Font atlases and their fonts: TTF data or Nuklear's built-in ProggyClean
   baked into one RGBA image, kept for a renderer to upload. */

#include "ui_internal.h"

#include <math.h>

ui_table ui_atlases = { "font atlas" };
ui_table ui_fonts = { "font" };

ui_font *ui_font_get(gs_ui_font f, const char *fn) {
    return (ui_font *)ui_table_get(&ui_fonts, f.id, fn);
}

gs_ui_font ui_font_handle(const struct nk_user_font *font) {
    gs_ui_font h = { 0 };
    for (uint32_t i = 0; font && i < ui_fonts.count; i++) {
        ui_font *f = (ui_font *)ui_table_at(&ui_fonts, i);
        if (f && &f->font->handle == font) {
            h.id = ((uint32_t)ui_fonts.gens[i] << UI_INDEX_BITS) | i;
            break;
        }
    }
    return h;
}

static ui_atlas *ui_atlas_get(gs_ui_font_atlas a, const char *fn) {
    return (ui_atlas *)ui_table_get(&ui_atlases, a.id, fn);
}

gs_ui_font_atlas gs_ui_create_font_atlas(void) {
    gs_ui_font_atlas h = { 0 };
    ui_atlas *a = (ui_atlas *)calloc(1, sizeof *a);
    if (!a) {
        ui_fail("out of memory for a font atlas");
        return h;
    }
    nk_font_atlas_init_default(&a->atlas);
    nk_font_atlas_begin(&a->atlas);
    a->scale = 1;
    a->id = ui_table_add(&ui_atlases, a);
    if (!a->id) {
        free(a);
        return h;
    }
    h.id = a->id;
    return h;
}

/* Whether a context still uses one of the atlas's fonts: the one it was made
   with (whose white texel draws its shapes), or one its style or a saved
   style names. */
bool ui_atlas_in_use(ui_atlas *a) {
    for (uint32_t i = 0; i < ui_contexts.count; i++) {
        ui_ctx *u = (ui_ctx *)ui_table_at(&ui_contexts, i);
        if (!u) continue;
        if (u->atlas == a->id) return true;
        const struct nk_config_stack_user_font *fs = &u->nk.stacks.fonts;
        for (int k = -1; k < fs->head; k++) {
            const struct nk_user_font *font = k < 0 ? u->nk.style.font : fs->elements[k].old_value;
            ui_font *f = (ui_font *)ui_table_find(&ui_fonts, ui_font_handle(font).id);
            if (f && f->atlas == a->id) return true;
        }
    }
    return false;
}

void gs_ui_destroy_font_atlas(gs_ui_font_atlas h) {
    ui_atlas *a = ui_atlas_get(h, "ui::destroy");
    if (!a) return;
    if (ui_atlas_in_use(a)) {
        ui_misuse("ui::destroy: a context still uses a font of this atlas; destroy the context "
                  "first");
        return;
    }
    ui_free_atlas(a);
}

/* Textures that held a destroyed atlas's image, until a renderer takes the
   list to release its own among them. */
static uint32_t *ui_released;
static int64_t ui_nreleased, ui_released_cap;

int64_t gs_ui_released_textures(gs_ui_u32_slice out) {
    int64_t n = ui_nreleased;
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, ui_released, sizeof(uint32_t) * (size_t)k);
    /* Taken once they all fit: a call with no room asks for the count. */
    if (out.len >= n) ui_nreleased = 0;
    return n;
}

void ui_free_atlas(ui_atlas *a) {
    if (a->texture) {
        if (ui_nreleased == ui_released_cap) {
            int64_t cap = ui_released_cap ? ui_released_cap * 2 : 8;
            uint32_t *more = (uint32_t *)realloc(ui_released, sizeof(uint32_t) * (size_t)cap);
            if (more) {
                ui_released = more;
                ui_released_cap = cap;
            }
        }
        if (ui_nreleased < ui_released_cap) ui_released[ui_nreleased++] = a->texture;
    }
    for (int i = 0; i < a->nfonts; i++) {
        free(ui_table_find(&ui_fonts, a->fonts[i]));
        ui_table_remove(&ui_fonts, a->fonts[i]);
    }
    ui_table_remove(&ui_atlases, a->id);
    nk_font_atlas_clear(&a->atlas);
    for (int i = 0; i < a->nranges; i++) free(a->ranges[i]);
    free(a->ranges);
    free(a->fonts);
    free(a->pixels);
    free(a);
}

uint8_t gs_ui_font_atlas_is_valid(gs_ui_font_atlas a) {
    return ui_table_find(&ui_atlases, a.id) != NULL;
}

gs_ui_font_config gs_ui_default_font_config(void) {
    struct nk_font_config n = nk_font_config(13.0f);
    gs_ui_font_config g;
    memset(&g, 0, sizeof g);
    g.oversample_h = n.oversample_h;
    g.oversample_v = n.oversample_v;
    g.pixel_snap = n.pixel_snap;
    g.merge_mode = n.merge_mode;
    g.spacing = ui_float2_of(n.spacing);
    g.ranges = GS_UI_RANGE_DEFAULT;
    g.fallback_glyph = n.fallback_glyph;
    return g;
}

static const nk_rune *ui_builtin_ranges(int64_t which) {
    switch (which) {
        case GS_UI_RANGE_DEFAULT: return nk_font_default_glyph_ranges();
        case GS_UI_RANGE_CHINESE: return nk_font_chinese_glyph_ranges();
        case GS_UI_RANGE_CYRILLIC: return nk_font_cyrillic_glyph_ranges();
        case GS_UI_RANGE_KOREAN: return nk_font_korean_glyph_ranges();
        default: return NULL;
    }
}

int64_t gs_ui_font_glyph_ranges(int64_t which, gs_ui_u32_slice out) {
    const nk_rune *r = ui_builtin_ranges(which);
    if (!r) {
        ui_misuse("ui::font_glyph_ranges: %lld is not one of the RANGE_* constants",
                  (long long)which);
        return 0;
    }
    int64_t n = 0;
    while (r[n]) n++;
    for (int64_t i = 0; i < n && i < out.len; i++) out.data[i] = r[i];
    return n;
}

/* Whether `data` starts like a TrueType or OpenType font whose table
   directory lies inside it: stb_truetype, which Nuklear bakes with, reads
   the tables it names without bounds checks. Past that a font's data is
   trusted, as an image's is. */
static bool ui_ttf_plausible(const uint8_t *data, int64_t len) {
    if (len < 12) return false;
    uint32_t tag = (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 |
                   data[3];
    if (tag != 0x00010000u && tag != 0x74727565u /* true */ && tag != 0x4F54544Fu /* OTTO */)
        return false;
    int64_t tables = (int64_t)data[4] << 8 | data[5];
    if (12 + tables * 16 > len) return false;
    for (int64_t i = 0; i < tables; i++) {
        const uint8_t *t = data + 12 + i * 16;
        int64_t offset = (int64_t)t[8] << 24 | (int64_t)t[9] << 16 | (int64_t)t[10] << 8 | t[11];
        int64_t size = (int64_t)t[12] << 24 | (int64_t)t[13] << 16 | (int64_t)t[14] << 8 | t[15];
        if (offset > len || size > len - offset) return false;
    }
    return true;
}

uint8_t *ui_read_font_file(const char *path, int64_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        ui_fail("cannot open font file %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = size > 0 ? (uint8_t *)malloc((size_t)size) : NULL;
    bool read = bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!read || !ui_ttf_plausible(bytes, size)) {
        free(bytes);
        ui_fail("%s is not a TrueType or OpenType font", path);
        return NULL;
    }
    *len = size;
    return bytes;
}

gs_ui_font ui_add_font(gs_ui_font_atlas h, int source, gs_ui_bytes data, float height,
                       gs_ui_font_config config, gs_ui_u32_slice ranges, const char *fn) {
    const gs_ui_font_config *g = &config;
    gs_ui_font none = { 0 };
    ui_atlas *a = ui_atlas_get(h, fn);
    if (!a) return none;
    if (a->baked) {
        ui_misuse("%s: the atlas is baked already; fonts are added before ui::bake", fn);
        return none;
    }
    if (!(height >= 1 && height <= 1000)) {
        ui_misuse("%s: a font %g pixels high (1 to 1000)", fn, (double)height);
        return none;
    }
    if (g->oversample_h < 1 || g->oversample_h > 8 || g->oversample_v < 1 || g->oversample_v > 8 ||
        !isfinite(g->spacing.x) || !isfinite(g->spacing.y)) {
        ui_misuse("%s: oversampling of %d x %d (1 to 8 each)", fn, g->oversample_h,
                  g->oversample_v);
        return none;
    }
    if (g->merge_mode && !a->nfonts) {
        ui_misuse("%s: merge_mode adds glyphs to the font added before, and the atlas has none",
                  fn);
        return none;
    }
    /* The characters to bake, zero-terminated pairs; Nuklear reads them for
       as long as the font exists, so custom ones are copied into the atlas. */
    const nk_rune *range;
    if (ranges.len) {
        if (ranges.len % 2) {
            ui_misuse("%s: %lld glyph range bounds, where ranges come in pairs", fn,
                      (long long)ranges.len);
            return none;
        }
        int64_t glyphs = 0;
        for (int64_t i = 0; i < ranges.len; i += 2) {
            uint32_t lo = ranges.data[i], hi = ranges.data[i + 1];
            if (!lo || hi < lo || hi > 0x10FFFF) {
                ui_misuse("%s: a glyph range from %u to %u", fn, lo, hi);
                return none;
            }
            glyphs += hi - lo + 1;
        }
        if (glyphs > 65536) {
            ui_misuse("%s: ranges of %lld glyphs, where a font may have 65536", fn,
                      (long long)glyphs);
            return none;
        }
        nk_rune *copy = (nk_rune *)malloc(sizeof(nk_rune) * (size_t)(ranges.len + 1));
        memcpy(copy, ranges.data, sizeof(nk_rune) * (size_t)ranges.len);
        copy[ranges.len] = 0;
        a->ranges = (nk_rune **)realloc(a->ranges, sizeof(nk_rune *) * (size_t)(a->nranges + 1));
        a->ranges[a->nranges++] = copy;
        range = copy;
    } else {
        range = ui_builtin_ranges(g->ranges);
        if (!range) {
            ui_misuse("%s: FontConfig.ranges %d is not one of the RANGE_* constants", fn,
                      g->ranges);
            return none;
        }
    }
    /* Characters the font lacks are drawn as the fallback, which has to be
       one it has. */
    if (!g->merge_mode) {
        bool found = false;
        for (const nk_rune *r = range; r[0] && r[1]; r += 2)
            found = found || (g->fallback_glyph >= r[0] && g->fallback_glyph <= r[1]);
        if (!found) {
            ui_misuse("%s: the fallback glyph %u is not in the font's ranges", fn,
                      g->fallback_glyph);
            return none;
        }
    }
    struct nk_font_config cfg = nk_font_config(height);
    cfg.oversample_h = g->oversample_h;
    cfg.oversample_v = g->oversample_v;
    cfg.pixel_snap = g->pixel_snap != 0;
    cfg.merge_mode = g->merge_mode != 0;
    cfg.spacing = ui_nk_vec2(g->spacing);
    cfg.range = range;
    cfg.fallback_glyph = g->fallback_glyph;
    struct nk_font *font = NULL;
    if (source == UI_FONT_DEFAULT) {
        font = nk_font_atlas_add_default(&a->atlas, height, &cfg);
    } else if (source == UI_FONT_MEMORY) {
        if (!ui_ttf_plausible(data.data, data.len)) {
            ui_misuse("%s: %lld bytes that are not a TrueType or OpenType font", fn,
                      (long long)data.len);
            return none;
        }
        /* Nuklear copies the data into the atlas. */
        font = nk_font_atlas_add_from_memory(&a->atlas, data.data, (nk_size)data.len, height, &cfg);
    } else {
        int64_t size = 0;
        uint8_t *bytes = ui_read_font_file(ui_cstr(data, 0), &size);
        if (!bytes) return none;
        font = nk_font_atlas_add_from_memory(&a->atlas, bytes, (nk_size)size, height, &cfg);
        free(bytes);
    }
    if (cfg.merge_mode) {
        /* Nuklear merges into the atlas's first font, whatever was added
           after it: that font is the handle. */
        gs_ui_font first = { a->fonts[0] };
        return first;
    }
    if (!font) {
        ui_fail("%s: Nuklear could not add the font", fn);
        return none;
    }
    ui_font *f = (ui_font *)calloc(1, sizeof *f);
    f->font = font;
    f->atlas = a->id;
    uint32_t id = ui_table_add(&ui_fonts, f);
    if (!id) {
        free(f);
        return none;
    }
    a->fonts = (uint32_t *)realloc(a->fonts, sizeof(uint32_t) * (size_t)(a->nfonts + 1));
    a->fonts[a->nfonts++] = id;
    gs_ui_font h2 = { id };
    return h2;
}

gs_ui_font gs_ui_add_default_font(gs_ui_font_atlas a, float height,
                                  gs_ui_font_config config, gs_ui_u32_slice ranges) {
    gs_ui_bytes none = { NULL, 0 };
    return ui_add_font(a, UI_FONT_DEFAULT, none, height, config, ranges, "ui::add_default_font");
}

gs_ui_font gs_ui_add_font_from_memory(gs_ui_font_atlas a, gs_ui_bytes ttf, float height,
                                      gs_ui_font_config config, gs_ui_u32_slice ranges) {
    return ui_add_font(a, UI_FONT_MEMORY, ttf, height, config, ranges, "ui::add_font_from_memory");
}

gs_ui_font gs_ui_add_font_from_file(gs_ui_font_atlas a, gs_ui_bytes path, float height,
                                    gs_ui_font_config config, gs_ui_u32_slice ranges) {
    return ui_add_font(a, UI_FONT_FILE, path, height, config, ranges, "ui::add_font_from_file");
}

/* Stamps `texture` on everything that draws from the atlas's image. */
static void ui_stamp_texture(ui_atlas *a, uint32_t texture) {
    nk_handle h = nk_handle_id((int)texture);
    for (struct nk_font *f = a->atlas.fonts; f; f = f->next) {
        f->texture = h;
        f->handle.texture = h;
    }
    a->texture = texture;
}

uint8_t gs_ui_bake_font_atlas(gs_ui_font_atlas h, float scale) {
    ui_atlas *a = ui_atlas_get(h, "ui::bake");
    if (!a) return 0;
    if (a->baked) return ui_misuse("ui::bake: the atlas is baked already");
    if (!a->nfonts)
        return ui_misuse("ui::bake: the atlas has no fonts (ui::add_default_font adds one)");
    if (!ui_scale_ok(scale, "ui::bake")) return 0;
    /* The glyphs at `scale` times their fonts' size, merged ones too, each
       font measuring at its own size after (below): sharp text for a ui
       drawn that many times bigger. */
    for (struct nk_font_config *c = a->atlas.config; c; c = c->next) {
        struct nk_font_config *it = c;
        do {
            it->size *= scale;
            it = it->n;
        } while (it != c);
    }
    int w = 0, hgt = 0;
    const void *image = nk_font_atlas_bake(&a->atlas, &w, &hgt, NK_FONT_ATLAS_RGBA32);
    if (!image) return ui_fail("the font atlas could not be baked: a font is damaged, or its "
                               "glyphs do not fit");
    size_t size = (size_t)w * (size_t)hgt * 4;
    a->pixels = (unsigned char *)malloc(size);
    memcpy(a->pixels, image, size);
    a->width = w;
    a->height = hgt;
    /* Nuklear's end of baking frees its copy of the image and finds the white
       texel; the texture comes later, from whoever uploads the pixels. */
    struct nk_draw_null_texture null_tex;
    nk_font_atlas_end(&a->atlas, nk_handle_id(0), &null_tex);
    nk_font_atlas_cleanup(&a->atlas);
    /* Nuklear scales a glyph by the font's height over the height it was
       baked at. */
    for (int i = 0; i < a->nfonts; i++) {
        ui_font *f = (ui_font *)ui_table_find(&ui_fonts, a->fonts[i]);
        f->font->handle.height = f->font->info.height / scale;
    }
    a->scale = scale;
    a->null_uv = null_tex.uv;
    a->baked = true;
    ui_stamp_texture(a, 0);
    return 1;
}

uint8_t gs_ui_font_atlas_is_baked(gs_ui_font_atlas h) {
    ui_atlas *a = ui_atlas_get(h, "ui::is_baked");
    return a && a->baked;
}

void gs_ui_font_atlas_size(gs_ui_font_atlas h, gs_ui_int2 *out) {
    out->x = out->y = 0;
    ui_atlas *a = ui_atlas_get(h, "ui::atlas_size");
    if (!a) return;
    out->x = a->width;
    out->y = a->height;
}

int64_t gs_ui_font_atlas_pixels(gs_ui_font_atlas h, gs_ui_bytes out) {
    ui_atlas *a = ui_atlas_get(h, "ui::atlas_pixels");
    if (!a) return 0;
    return ui_copy_out(a->pixels, (int64_t)a->width * a->height * 4, out);
}

void gs_ui_set_font_atlas_texture(gs_ui_font_atlas h, uint32_t texture) {
    ui_atlas *a = ui_atlas_get(h, "ui::set_texture");
    if (!a) return;
    if (!a->baked) {
        ui_misuse("ui::set_texture: the atlas is not baked yet");
        return;
    }
    ui_stamp_texture(a, texture);
}

uint32_t gs_ui_font_atlas_texture(gs_ui_font_atlas h) {
    ui_atlas *a = ui_atlas_get(h, "ui::texture");
    return a ? a->texture : 0;
}

gs_ui_font_atlas gs_ui_font_atlas_without_texture(void) {
    gs_ui_font_atlas h = { 0 };
    for (uint32_t i = 0; i < ui_atlases.count; i++) {
        ui_atlas *a = (ui_atlas *)ui_table_at(&ui_atlases, i);
        if (a && a->baked && !a->texture) {
            h.id = a->id;
            break;
        }
    }
    return h;
}

void gs_ui_forget_font_atlas_textures(void) {
    for (uint32_t i = 0; i < ui_atlases.count; i++) {
        ui_atlas *a = (ui_atlas *)ui_table_at(&ui_atlases, i);
        if (a && a->baked) ui_stamp_texture(a, 0);
    }
}

int64_t gs_ui_font_atlas_fonts(gs_ui_font_atlas h, gs_ui_font_slice out) {
    ui_atlas *a = ui_atlas_get(h, "ui::fonts");
    if (!a) return 0;
    for (int64_t i = 0; i < a->nfonts && i < out.len; i++) out.data[i].id = a->fonts[i];
    return a->nfonts;
}

gs_ui_font_atlas gs_ui_font_atlas_of(gs_ui_font h) {
    gs_ui_font_atlas none = { 0 };
    ui_font *f = ui_font_get(h, "ui::atlas");
    if (!f) return none;
    gs_ui_font_atlas a = { f->atlas };
    return a;
}

uint8_t gs_ui_font_is_valid(gs_ui_font f) {
    return ui_table_find(&ui_fonts, f.id) != NULL;
}

/* A font whose atlas is baked, which a query needs its glyphs for. */
static ui_font *ui_baked_font(gs_ui_font h, const char *fn) {
    ui_font *f = ui_font_get(h, fn);
    if (!f) return NULL;
    ui_atlas *a = (ui_atlas *)ui_table_find(&ui_atlases, f->atlas);
    if (!a->baked) {
        ui_misuse("%s: the font's atlas is not baked yet", fn);
        return NULL;
    }
    return f;
}

gs_ui_font_info gs_ui_font_info_of(gs_ui_font h) {
    gs_ui_font_info info;
    memset(&info, 0, sizeof info);
    ui_font *f = ui_baked_font(h, "ui::info");
    if (!f) return info;
    /* Metrics at the font's own size, as Nuklear lays it out, whatever
       scale it was baked at. */
    float k = f->font->handle.height / f->font->info.height;
    info.height = f->font->handle.height;
    info.ascent = f->font->info.ascent * k;
    info.descent = f->font->info.descent * k;
    info.glyph_count = (int32_t)f->font->info.glyph_count;
    info.fallback_codepoint = f->font->fallback_codepoint;
    return info;
}

uint8_t gs_ui_font_find_glyph(gs_ui_font h, uint32_t codepoint, gs_ui_font_glyph *out) {
    memset(out, 0, sizeof *out);
    ui_font *f = ui_baked_font(h, "ui::find_glyph");
    if (!f) return 0;
    const struct nk_font_glyph *g = nk_font_find_glyph(f->font, codepoint);
    if (!g) return 0;
    float k = f->font->handle.height / f->font->info.height;
    out->codepoint = g->codepoint;
    out->xadvance = g->xadvance * k;
    out->x0 = g->x0 * k, out->y0 = g->y0 * k, out->x1 = g->x1 * k, out->y1 = g->y1 * k;
    out->w = g->w * k, out->h = g->h * k;
    out->u0 = g->u0, out->v0 = g->v0, out->u1 = g->u1, out->v1 = g->v1;
    /* The fallback stands in for a character the font lacks. */
    return g->codepoint == codepoint;
}

float gs_ui_font_text_width(gs_ui_font h, gs_ui_bytes text) {
    ui_font *f = ui_baked_font(h, "ui::text_width");
    if (!f) return 0;
    const struct nk_user_font *u = &f->font->handle;
    return u->width(u->userdata, u->height, (const char *)text.data,
                    ui_len(text, "ui::text_width"));
}
