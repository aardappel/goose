// The ui module's library, font atlases and fonts, and contexts: Nuklear's
// font configuration defaults against the Goose ones, the built-in font at
// two sizes and a TrueType one from a file and from memory in one atlas,
// glyph ranges of every kind, baking and what it leaves (the image, the
// fonts' metrics, glyphs, text widths), baking at a scale, the texture
// bookkeeping a renderer uses, contexts made on a font, by default and on
// a TrueType font of their own, and what each keeps.
import std;
import vec;
import math;
import os;
import ui;

fn num(x: f32) -> u8[>..] {
    let k = round(x as f64 * 100.0) as i64;
    var s: u8[>..] = [];
    if k < 0 { s.push('-'); }
    format_int(s, abs(k) / 100, 10, 0, ' ');
    s.push('.');
    format_int(s, abs(k) % 100, 10, 2, '0');
    return s;
}

// Nuklear's own copy of ProggyTiny, from the submodule the layer is built from:
// the suite runs from the repository's top.
let FONT_FILE = "third_party/nuklear/extra_font/ProggyTiny.ttf";

fn main() {
    print("available ", ui::available(), "; misuses ", ui::misuse_count(), "; error '",
          ui::error(), "'");
    ui::check();
    print("config defaults are Nuklear's: ", ui::default_font_config() == ui::FontConfig {});

    // Every built-in set of characters, as pairs, and the default one's
    // first pair: printable ASCII.
    for which in 4 {
        let r = ui::glyph_ranges(which);
        print("range set ", which, ": ", r.len / 2, " ranges, the first ", r[0], "..", r[1]);
    }

    let atlas = ui::create_font_atlas();
    print("atlas valid ", ui::is_valid(atlas), ", baked ", ui::is_baked(atlas), ", fonts ",
          ui::fonts(atlas).len);
    let small = ui::add_default_font(atlas, 13.0);
    let big = ui::add_default_font(atlas, 26.0, ui::FontConfig { oversample_h: 2, pixel_snap: true,
                                                                 spacing: float2 { 1.0, 0.0 } });
    // Only digits and the fallback '?' in this one, given as ranges.
    let digits_only: u32[4] = [48, 57, 63, 63];
    let digits = ui::add_default_font(atlas, 20.0, ui::FontConfig {}, digits_only);
    var ttf: u8[>..] = [];
    let read = read_file(FONT_FILE, ttf);
    let tiny_mem = ui::add_font_from_memory(atlas, ttf, 10.0);
    let tiny_file = ui::add_font_from_file(atlas, FONT_FILE, 10.0,
                                           ui::FontConfig { ranges: ui::RANGE_CYRILLIC });
    // Merged glyphs go into the atlas's first font, whose handle comes back.
    let merged = ui::add_font_from_memory(atlas, ttf, 13.0, ui::FontConfig { merge_mode: true },
                                          digits_only);
    print("read the font file ", read, " (", ttf.len > 0, "); fonts valid ", ui::is_valid(small),
          " ", ui::is_valid(big), " ", ui::is_valid(digits), " ", ui::is_valid(tiny_mem), " ",
          ui::is_valid(tiny_file), "; merged into the first ", merged == small);
    let all = ui::fonts(atlas);
    print("fonts in order: ", all.len, " ", all[0] == small, " ", all[1] == big, " ",
          all[2] == digits, " ", all[3] == tiny_mem, " ", all[4] == tiny_file,
          "; each in the atlas ", ui::atlas(tiny_file) == atlas);

    // A file that is not there fails with the reason, and so does a file
    // that is not a font; neither is a misuse.
    let missing = ui::add_font_from_file(atlas, "no_such_font.ttf", 12.0);
    print("missing file: handle ", missing.id, ", error '", ui::error(), "'");
    let not_font = ui::add_font_from_file(atlas, "test/ui/ui_fonts.goose", 12.0);
    print("not a font: handle ", not_font.id, ", error '", ui::error(), "'; misuses ",
          ui::misuse_count());

    guard ui::bake(atlas) else { abort(str("bake: ", ui::error())); }
    let size = ui::atlas_size(atlas);
    let px = ui::atlas_pixels(atlas);
    print("baked ", ui::is_baked(atlas), ": ", size.x, " wide, ", size.y > 0, " high, ", px.len,
          " bytes = w x h x 4 ", px.len == size.x as i64 * size.y as i64 * 4);
    // Glyphs are white with coverage in alpha.
    var opaque = 0;
    var white = true;
    for i in px.len / 4 {
        if px[i * 4 + 3] == 255 { opaque++; }
        if px[i * 4 + 3] > 0 && (px[i * 4] != 255 || px[i * 4 + 1] != 255 || px[i * 4 + 2] != 255) {
            white = false;
        }
    }
    print("opaque texels ", opaque > 100, "; all white ", white);

    for f in [small, big, digits, tiny_mem] {
        let info = ui::info(f);
        print("font ", num(info.height), " high: ascent ", num(info.ascent), ", descent ",
              num(info.descent), ", ", info.glyph_count, " glyphs, fallback ",
              info.fallback_codepoint, "; 'Goose' ", num(ui::text_width(f, "Goose")), " wide");
    }
    print("wider when bigger ", ui::text_width(big, "Goose") > ui::text_width(small, "Goose"),
          "; empty text ", num(ui::text_width(small, "")));
    let g, own = ui::find_glyph(small, 'A');
    print("'A': own ", own, ", codepoint ", g.codepoint, ", advance ", num(g.xadvance),
          ", size ", num(g.w), " x ", num(g.h), ", in the image ", g.u1 > g.u0 && g.v1 > g.v0);
    let q, qown = ui::find_glyph(digits, 'A');
    print("'A' in the digits font: own ", qown, ", drawn as ", q.codepoint);
    let d, down = ui::find_glyph(digits, '7');
    print("'7' in the digits font: own ", down, ", codepoint ", d.codepoint);
    let merged_glyph, merged_own = ui::find_glyph(small, '5');
    print("'5' in the first font, merged digits too: own ", merged_own, ", ",
          merged_glyph.codepoint);

    // The same font baked at two scales measures at its own size either way,
    // with twice the texels to each glyph at 2.
    let scales: f32[2] = [1.0, 2.0];
    for s in scales {
        let a = ui::create_font_atlas();
        let f = ui::add_default_font(a, 13.0);
        guard ui::bake(a, s) else { abort(str("bake: ", ui::error())); }
        let info = ui::info(f);
        let image = ui::atlas_size(a);
        let glyph, found = ui::find_glyph(f, 'A');
        print("baked at ", num(s), ": ", num(info.height), " high, ascent ", num(info.ascent),
              ", 'Goose' ", num(ui::text_width(f, "Goose")), " wide; 'A' ", found, " ",
              num(glyph.w), " x ", num(glyph.h), " from ",
              num((glyph.u1 - glyph.u0) * image.x as f32), " x ",
              num((glyph.v1 - glyph.v0) * image.y as f32), " texels");
        ui::destroy(a);
    }

    // The texture a renderer uploads the image to: none yet, so the atlas is
    // the one without; set, forgotten, set again.
    print("texture ", ui::texture(atlas), "; waiting ", ui::atlas_without_texture() == atlas);
    ui::set_texture(atlas, 77);
    print("texture ", ui::texture(atlas), "; waiting ", ui::atlas_without_texture().id);
    ui::forget_textures();
    print("forgotten: texture ", ui::texture(atlas), "; waiting ",
          ui::atlas_without_texture() == atlas);
    ui::set_texture(atlas, 78);

    // A context on one of the fonts, and one of its own.
    let ctx = ui::create_context(big);
    print("context valid ", ui::is_valid(ctx), "; font ", ui::font(ctx) == big, "; atlas ",
          ui::context_atlas(ctx) == atlas, "; null texture ", ui::null_texture(ctx));
    print("delta time ", num(ui::delta_time(ctx)));
    ui::set_delta_time(ctx, 0.25);
    print("delta time ", num(ui::delta_time(ctx)));
    // Nothing copied yet; a paste text stays for the edits that paste it.
    let copied, any = ui::copied(ctx);
    print("copied '", copied, "' ", any);
    ui::set_clipboard(ctx, "pasted text");

    let own_ctx = ui::create(16.0);
    let own_atlas = ui::context_atlas(own_ctx);
    print("default context: valid ", ui::is_valid(own_ctx), ", atlas ", ui::is_valid(own_atlas),
          " baked ", ui::is_baked(own_atlas), " with ", ui::fonts(own_atlas).len, " font of ",
          num(ui::info(ui::font(own_ctx)).height));
    // Its atlas goes with it.
    ui::destroy(own_ctx);
    print("destroyed: context ", ui::is_valid(own_ctx), ", its atlas ", ui::is_valid(own_atlas));

    // Contexts of a TrueType font of their own, from a file or from its
    // bytes, which set_scale bakes again: a new font, the same size.
    let file_ctx = ui::create_from_file(FONT_FILE, 12.0);
    let from_file = ui::font(file_ctx);
    print("from a file: valid ", ui::is_valid(file_ctx), ", ", num(ui::info(from_file).height),
          " high, 'Goose' ", num(ui::text_width(from_file, "Goose")), " wide");
    ui::set_scale(file_ctx, 2.0);
    let rebaked = ui::font(file_ctx);
    print("at 2: a new font ", rebaked != from_file, ", the old one gone ",
          !ui::is_valid(from_file), ", ", num(ui::info(rebaked).height), " high, 'Goose' ",
          num(ui::text_width(rebaked, "Goose")), " wide");
    let memory_ctx = ui::create_from_memory(ttf, 12.0);
    print("from memory: valid ", ui::is_valid(memory_ctx), ", ",
          num(ui::info(ui::font(memory_ctx)).height), " high");
    let no_ctx = ui::create_from_file("no_such_font.ttf", 12.0);
    print("from a missing file: handle ", no_ctx.id, ", error '", ui::error(), "'");
    let file_atlas = ui::context_atlas(file_ctx);
    ui::destroy(file_ctx);
    ui::destroy(memory_ctx);
    print("destroyed: the file context's atlas ", ui::is_valid(file_atlas));

    // A destroyed atlas's texture comes back once, for the renderer that
    // made it to release; an atlas without one adds nothing.
    let textured = ui::create_font_atlas();
    ui::add_default_font(textured, 13.0);
    ui::bake(textured);
    ui::set_texture(textured, 91);
    let bare = ui::create_font_atlas();
    ui::destroy(textured);
    ui::destroy(bare);
    let released = ui::released_textures();
    print("released textures: ", released.len, ", ", released[0], "; then ",
          ui::released_textures().len);

    ui::destroy(ctx);
    ui::destroy(atlas);
    print("all destroyed: ", ui::is_valid(ctx), " ", ui::is_valid(atlas), " ", ui::is_valid(big),
          "; misuses ", ui::misuse_count());
    ui::check();
}
