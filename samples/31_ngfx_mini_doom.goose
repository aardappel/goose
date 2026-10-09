// 31_mini_doom on ngfx (docs/stdlib.md): the same game, drawn the way
// NoGraphicsAPI draws. The world and HUD meshes are rebuilt every frame, so
// they live in that frame's memory (ngfx::frame_data) and the shaders fetch
// their vertices through pointers in their roots; the atlas and its sampler
// are indices in those roots. Everything else is 31_mini_doom's.
//
// A small Doom-like game, and a tour of loading real binary game assets.
// The supplied Freedoom WAD supplies the map, palette, textures, sprites,
// font and sound effects. This is an independent, deliberately small game;
// it is not a source port and does not reproduce Doom's simulation exactly.
//
//   goose samples/31_ngfx_mini_doom.goose
//   goose samples/31_ngfx_mini_doom.goose -- --test
//   goose samples/31_ngfx_mini_doom.goose -- --test --frames 30
//   goose samples/31_ngfx_mini_doom.goose -- --wad path/to/freedoom1-e1m1.wad
//
// WASD walks, Shift runs, mouse or Left/Right turns, E or Space uses doors
// and switches. Left mouse or Ctrl fires. 1 selects fists, 2 the pistol,
// 3 the shotgun, 4 the chaingun. Tab shows the map; H toggles help.
// Escape pauses and releases the mouse; click or Escape resumes. Losing
// focus pauses too. R restarts after death or finishing. F12 saves a PNG.
// Starts straight in E1M1. Find the blue key and reach the exit switch.
//
// Included: solid walls, stairs, falling, lifts, locked/secret doors, sector
// lighting, animated liquids, damaging floors, pickups, four enemy types,
// imp fireballs, exploding barrels, weapon/sprite animation and sound.
// Limits: medium difficulty only, simple pursuit (no pathfinding), frontal
// monster sprites, approximate lighting/combat, no music, saves, multiplayer,
// jumping or vertical mouse look. Other weapons become useful ammo pickups.
// Only this map's line specials are implemented. This is not a WAD browser.
//
// Read downwards: bytes -> pictures -> map -> movement -> game -> rendering.
// Plain loops and small records do most of the work. Collision is done in
// the map's two-dimensional plane; a separate physics world is unnecessary.

import std;
import math;
import vec;
import os;
import binary;
import ngfx;
import audio;

// --- the WAD directory and little-endian values ------------------------------

struct Lump { name: u8[..8], start: i64, size: i64 }
var wad: u8[>..] = [];
var lumps: Lump[>..] = [];

fn require(ok: bool, message: const u8[:]) {
    if !ok { abort(str("mini doom: ", message)); }
}
// The library returns recoverable failures. This sample adds WAD context and
// stops loading; byte order, range checks and name trimming live in binary.
fn u16le(data: const u8[:], at: i64) -> i64 {
    let value, ok = binary::read_u16le(data, at);
    if !ok { abort(str("mini doom: truncated 16-bit WAD field at byte ", at)); }
    value
}
fn i16le(data: const u8[:], at: i64) -> i64 {
    let value, ok = binary::read_i16le(data, at);
    if !ok { abort(str("mini doom: truncated signed 16-bit WAD field at byte ", at)); }
    value
}
fn u32le(data: const u8[:], at: i64) -> i64 {
    let value, ok = binary::read_u32le(data, at);
    if !ok { abort(str("mini doom: truncated 32-bit WAD field at byte ", at)); }
    value
}
fn name_at(data: const u8[:], at: i64) -> u8[..8] {
    let bytes, ok = binary::read_name(data, at, 8);
    if !ok { abort(str("mini doom: truncated WAD name at byte ", at)); }
    var name: u8[..8] = [];
    name.append(bytes);
    name
}
fn find_lump(name: const u8[:]) -> i64 {
    // Last definition wins, as in a normal WAD directory.
    var i = lumps.len;
    while i > 0 {
        i--;
        if lumps[i].name == name { return i; }
    }
    -1
}
fn lump_bytes(i: i64) -> const u8[:] {
    require(i >= 0 && i < lumps.len, "missing WAD lump");
    let l = lumps[i];
    wad[l.start..l.start + l.size]
}
fn lump(name: const u8[:]) -> const u8[:] {
    let i = find_lump(name);
    require(i >= 0, str("missing lump ", name));
    lump_bytes(i)
}
fn load_wad(path: const u8[:]) {
    require(read_file(path, wad), str("cannot read ", path));
    var header = binary::reader(wad);
    let signature, signature_ok = binary::read_bytes(header, 4);
    let count_field, count_ok = binary::read_u32le(header);
    let directory, directory_ok = binary::read_u32le(header);
    require(signature_ok && count_ok && directory_ok, "truncated WAD header");
    let count: i64 = count_field;
    require(signature == "IWAD" || signature == "PWAD", "expected a Doom WAD");
    require(count <= 100000 && binary::range_ok(wad, directory, count * 16), "bad WAD directory");
    var file = binary::reader(wad);
    binary::skip(file, directory);
    var entries, entries_ok = binary::subreader(file, count * 16);
    require(entries_ok, "truncated WAD directory");
    for i in count {
        var entry, entry_ok = binary::subreader(entries, 16);
        let start, start_ok = binary::read_u32le(entry);
        let size, size_ok = binary::read_u32le(entry);
        let bytes, name_ok = binary::read_name(entry, 8);
        require(entry_ok && start_ok && size_ok && name_ok, "truncated WAD entry");
        require(binary::range_ok(wad, start, size), "lump extends past end of WAD");
        var name: u8[..8] = [];
        name.append(bytes);
        lumps.push(Lump { name, start, size });
    }
    require(find_lump("E1M1") >= 0, "this sample needs the supplied E1M1 WAD");
}

// --- palette pictures in one GPU atlas --------------------------------------

// A shelf packer suffices: each row is as tall as its tallest picture.
// Texture coordinates repeat *within* each rectangle in the fragment shader.
// Nearest filtering keeps the palette artwork crisp and avoids atlas bleeding.
let ATLAS_W = 2048;
let ATLAS_H = 4096;
struct Picture {
    name: u8[..8], kind: i64,       // 0: patch, 1: wall texture, 2: flat
    x: i64, y: i64, w: i64, h: i64,
    left: i64, top: i64,           // patch anchor (weapons and sprites)
}
var pictures: Picture[>..] = [];
var atlas_pixels: u8[>..] = [];
var shelf_x = 0;
var shelf_y = 0;
var shelf_height = 0;
var palette: u8[768];
var font: i64[128];
var sky_picture = 0;

fn find_picture(name: const u8[:], kind: i64) -> i64 {
    for i in pictures.len {
        if pictures[i].kind == kind && pictures[i].name == name { return i; }
    }
    -1
}
fn new_picture(name: const u8[:], kind: i64, w: i64, h: i64) -> i64 {
    require(w > 0 && h > 0 && w <= ATLAS_W && h <= 1024, "bad picture dimensions");
    if shelf_x + w > ATLAS_W {
        shelf_x = 0;
        shelf_y += shelf_height;
        shelf_height = 0;
    }
    require(shelf_y + h <= ATLAS_H, "picture atlas is full");
    var p = Picture { kind: kind, x: shelf_x, y: shelf_y, w: w, h: h, .. };
    p.name.append(name);
    let id = pictures.len;
    pictures.push(p);
    shelf_x += w;
    shelf_height = max(shelf_height, h);
    id
}
fn pixel(pic: i64, x: i64, y: i64, color: i64) {
    let p = pictures[pic];
    if x < 0 || y < 0 || x >= p.w || y >= p.h { return; }
    let at = ((p.y + y) * ATLAS_W + p.x + x) * 4;
    for c in 3 { atlas_pixels[at + c] = palette[color * 3 + c]; }
    atlas_pixels[at + 3] = 255;
}
fn paint_patch(pic: i64, data: const u8[:], origin_x: i64, origin_y: i64) {
    let w = u16le(data, 0);
    require(binary::range_ok(data, 8, w * 4), "truncated patch columns");
    for x in w {
        var at = u32le(data, 8 + x * 4);
        var previous = -1;
        loop {
            require(binary::range_ok(data, at, 1), "unterminated patch column");
            var top = data[at] as i64;
            if top == 255 { break; }
            require(binary::range_ok(data, at, 4), "truncated patch post");
            let count = data[at + 1] as i64;
            require(binary::range_ok(data, at, count + 4), "truncated patch pixels");
            // Also accepts the relative posts used by tall Doom patches.
            if top <= previous { top += previous; }
            previous = top;
            for y in count { pixel(pic, origin_x + x, origin_y + top + y, data[at + 3 + y]); }
            at += count + 4;
        }
    }
}
fn patch(name: const u8[:]) -> i64 {
    let cached = find_picture(name, 0);
    if cached >= 0 { return cached; }
    let data = lump(name);
    let id = new_picture(name, 0, u16le(data, 0), u16le(data, 2));
    pictures[id].left = i16le(data, 4);
    pictures[id].top = i16le(data, 6);
    paint_patch(id, data, 0, 0);
    id
}
fn flat(name: const u8[:]) -> i64 {
    if name == "F_SKY1" { return -1; }
    let cached = find_picture(name, 2);
    if cached >= 0 { return cached; }
    let data = lump(name);
    require(data.len == 4096, "flat must be 64 x 64");
    let id = new_picture(name, 2, 64, 64);
    for y in 64 { for x in 64 { pixel(id, x, y, data[y * 64 + x]); } }
    id
}
fn wall_texture(name: const u8[:]) -> i64 {
    if name == "-" { return -1; }
    let id = find_picture(name, 1);
    require(id >= 0, str("missing wall texture ", name));
    id
}
fn load_pictures() {
    atlas_pixels.resize(ATLAS_W * ATLAS_H * 4, 0);
    let pal = lump("PLAYPAL");
    require(pal.len >= 768, "truncated palette");
    for i in 768 { palette[i] = pal[i]; }
    // Picture 0 is an opaque white pixel, useful for HUD rectangles.
    new_picture("WHITE", 0, 1, 1);
    for i in 4 { atlas_pixels[i] = 255; }

    let names = lump("PNAMES");
    let name_count = u32le(names, 0);
    require(binary::range_ok(names, 4, name_count * 8), "truncated PNAMES");
    let textures = lump("TEXTURE1");
    let count = u32le(textures, 0);
    require(binary::range_ok(textures, 4, count * 4), "truncated texture directory");
    for i in count {
        let at = u32le(textures, 4 + i * 4);
        let name = name_at(textures, at);
        let id = new_picture(name, 1, u16le(textures, at + 12), u16le(textures, at + 14));
        let patches = u16le(textures, at + 20);
        require(binary::range_ok(textures, at + 22, patches * 10), "truncated texture patches");
        for j in patches {
            let p = at + 22 + j * 10;
            let number = u16le(textures, p + 4);
            require(number < name_count, "bad patch name index");
            let patch_name = name_at(names, 4 + number * 8);
            paint_patch(id, lump(patch_name), i16le(textures, p), i16le(textures, p + 2));
        }
    }
    // Load every sprite once. Gameplay only looks up these immutable IDs.
    let first = find_lump("S_START");
    let last = find_lump("S_END");
    require(first >= 0 && last > first, "missing sprite markers");
    for i in first + 1..last { if lumps[i].size > 0 { patch(lumps[i].name); } }
    for c in 33..96 {
        font[c] = patch(str("STCFN", if c < 100 { "0" } else { "" }, c));
    }
    patch("STBAR");
    patch("STFST00");
    patch("STFST10");
    patch("STFST20");
    patch("STFST30");
    patch("STFST40");
    patch("STFDEAD0");
    sky_picture = wall_texture("SKY1");
    for name in ["NUKAGE1", "NUKAGE2", "NUKAGE3", "FWATER1", "FWATER2", "FWATER3", "FWATER4"] {
        flat(name);
    }
}

// --- map records ------------------------------------------------------------

struct Side { offset: float2, upper: i64, lower: i64, middle: i64, sector: i64 }
struct Line {
    a: float2, b: float2, flags: i64, special: i64, tag: i64, front: i64, back: i64,
    activated: bool = false,
}
struct Sector {
    floor: f32, ceiling: f32, floor_pic: i64, ceiling_pic: i64,
    light: f32, special: i64, tag: i64,
    start_floor: f32, start_ceiling: f32,
    motion: i64, target: f32, wait: f32, speed: f32,
    secret_found: bool,
}
struct Seg { a: float2, b: float2, sector: i64 }
struct Subsector { first: i64, count: i64, sector: i64 }
struct Node { origin: float2, delta: float2, front: i64, back: i64 }
struct FloorTriangle { a: float2, b: float2, c: float2, sector: i64 }
var vertices: float2[>..] = [];
var sides: Side[>..] = [];
var lines: Line[>..] = [];
var sectors: Sector[>..] = [];
var segs: Seg[>..] = [];
var subsectors: Subsector[>..] = [];
var nodes: Node[>..] = [];
var floor_triangles: FloorTriangle[>..] = [];

fn cross2(a: float2, b: float2) -> f32 { a.x * b.y - a.y * b.x }
fn scale2(p: float2, s: f32) -> float2 { p * float2 { s, s } }
fn distance2(a: float2, b: float2) -> f32 { length(a - b) }
fn front_sector(line: Line) -> i64 { sides[line.front].sector }
fn back_sector(line: Line) -> i64 { if line.back < 0 { -1 } else { sides[line.back].sector } }

fn sector_at(p: float2) -> i64 {
    var child = nodes.len - 1;
    var steps = 0;
    while child < 32768 {
        require(child >= 0 && child < nodes.len && steps <= nodes.len, "invalid BSP tree");
        let n = nodes[child];
        child = if cross2(n.delta, p - n.origin) <= 0.0 { n.front } else { n.back };
        steps++;
    }
    let leaf = child - 32768;
    require(leaf < subsectors.len, "bad BSP leaf");
    subsectors[leaf].sector
}

// Original Doom subsectors list only their solid/portal edges, not all BSP
// cuts. A fan of seg endpoints leaves holes! Clip a bounding polygon down
// the BSP and then by the leaf's edges to recover each complete convex cell.
fn clip_polygon(poly: const float2[:], origin: float2, delta: float2) -> float2[..128] {
    // A bounded polygon also keeps recursive BSP frames fixed in size.
    require(poly.len < 128, "BSP polygon exceeds the sample's 128-corner limit");
    var out: float2[..128] = [];
    if poly.len == 0 { return out; }
    var previous = poly[poly.len - 1];
    var d0 = cross2(delta, previous - origin);
    for current in poly {
        let d1 = cross2(delta, current - origin);
        if (d0 <= 0.0) != (d1 <= 0.0) {
            out.push(previous + scale2(current - previous, d0 / (d0 - d1)));
        }
        if d1 <= 0.0 { out.push(current); }
        previous = current;
        d0 = d1;
    }
    out
}
recursive fn build_floors(child: i64, poly: const float2[:], depth: i64) {
    require(depth <= nodes.len, "cyclic BSP tree");
    if poly.len < 3 { return; }
    if child >= 32768 {
        require(child - 32768 < subsectors.len, "bad floor leaf");
        let leaf = subsectors[child - 32768];
        var clipped: float2[..128] = [];
        clipped.append(poly);
        for i in leaf.count {
            let edge = segs[leaf.first + i];
            let next = clip_polygon(clipped, edge.a, edge.b - edge.a);
            clipped.resize(0);
            clipped.append(next);
        }
        for i in 1..clipped.len - 1 {
            // Discard numerical slivers smaller than a twentieth of a
            // square map unit; their centroids can round across a BSP cut.
            if abs(cross2(clipped[i] - clipped[0], clipped[i + 1] - clipped[0])) > 0.1 {
                floor_triangles.push(FloorTriangle { clipped[0], clipped[i], clipped[i + 1], leaf.sector });
            }
        }
        return;
    }
    require(child >= 0 && child < nodes.len, "bad floor node");
    let n = nodes[child];
    let right = clip_polygon(poly, n.origin, n.delta);
    let left = clip_polygon(poly, n.origin, scale2(n.delta, -1.0));
    build_floors(n.front, right, depth + 1);
    build_floors(n.back, left, depth + 1);
}
fn records(name: const u8[:], size: i64) -> const u8[:] {
    let data = lump(name);
    require(data.len > 0 && data.len % size == 0, str("bad ", name, " record size"));
    data
}
fn load_map() {
    let v = records("VERTEXES", 4);
    for i in v.len / 4 { vertices.push(float2 { i16le(v, i * 4), i16le(v, i * 4 + 2) }); }
    let sec = records("SECTORS", 26);
    for i in sec.len / 26 {
        let at = i * 26;
        let bottom: f32 = i16le(sec, at);
        let top: f32 = i16le(sec, at + 2);
        sectors.push(Sector {
            floor: bottom, ceiling: top,
            floor_pic: flat(name_at(sec, at + 4)), ceiling_pic: flat(name_at(sec, at + 12)),
            light: clamp<f32>(i16le(sec, at + 20) / 255.0, 0.0, 1.0),
            special: i16le(sec, at + 22), tag: i16le(sec, at + 24),
            start_floor: bottom, start_ceiling: top, .. });
    }
    let sd = records("SIDEDEFS", 30);
    for i in sd.len / 30 {
        let at = i * 30;
        let sector = u16le(sd, at + 28);
        require(sector < sectors.len, "bad sidedef sector");
        sides.push(Side { float2 { i16le(sd, at), i16le(sd, at + 2) },
            wall_texture(name_at(sd, at + 4)), wall_texture(name_at(sd, at + 12)),
            wall_texture(name_at(sd, at + 20)), sector });
    }
    let ld = records("LINEDEFS", 14);
    for i in ld.len / 14 {
        let at = i * 14;
        let a = u16le(ld, at);
        let b = u16le(ld, at + 2);
        let front = u16le(ld, at + 10);
        let back = i16le(ld, at + 12);
        require(a < vertices.len && b < vertices.len, "bad linedef vertex");
        require(front < sides.len && back >= -1 && back < sides.len, "bad linedef side");
        lines.push(Line { vertices[a], vertices[b], u16le(ld, at + 4),
            u16le(ld, at + 6), u16le(ld, at + 8), front, back });
    }
    let sg = records("SEGS", 12);
    for i in sg.len / 12 {
        let at = i * 12;
        let a = u16le(sg, at);
        let b = u16le(sg, at + 2);
        let line = u16le(sg, at + 6);
        let direction = u16le(sg, at + 8);
        require(a < vertices.len && b < vertices.len && line < lines.len && direction <= 1, "bad seg");
        let side = if direction == 0 { lines[line].front } else { lines[line].back };
        require(side >= 0, "seg has no sidedef");
        segs.push(Seg { vertices[a], vertices[b], sides[side].sector });
    }
    let ss = records("SSECTORS", 4);
    for i in ss.len / 4 {
        let count = u16le(ss, i * 4);
        let first = u16le(ss, i * 4 + 2);
        require(count > 0 && first + count <= segs.len, "bad subsector");
        subsectors.push(Subsector { first, count, segs[first].sector });
    }
    let nd = records("NODES", 28);
    for i in nd.len / 28 {
        let at = i * 28;
        nodes.push(Node { float2 { i16le(nd, at), i16le(nd, at + 2) },
            float2 { i16le(nd, at + 4), i16le(nd, at + 6) },
            u16le(nd, at + 24), u16le(nd, at + 26) });
    }
    let bounds: float2[4] = [float2 { -32768.0, -32768.0 }, float2 { 32768.0, -32768.0 },
        float2 { 32768.0, 32768.0 }, float2 { -32768.0, 32768.0 }];
    build_floors(nodes.len - 1, bounds, 0);
}

// --- things, player and sound ------------------------------------------------

let STEP: f32 = 1.0 / 60.0;
let PLAYER_RADIUS: f32 = 16.0;
let PLAYER_HEIGHT: f32 = 56.0;
struct Player {
    pos: float2, z: f32, falling: f32, yaw: f32,
    health: i64, armor: i64, bullets: i64, shells: i64,
    weapon: i64, shotgun: bool, chaingun: bool, blue_key: bool,
    cooldown: f32, flash: f32, hurt: f32, bonus: f32, bob: f32,
}
struct Actor {
    pos: float2, z: f32, kind: i64, picture: i64,
    radius: f32, health: i64, alive: bool, solid: bool,
    awake: bool, timer: f32, pain: f32, death: f32, attacking: f32,
}
struct Fireball { pos: float2, z: f32, velocity: float2, vz: f32, life: f32 }
var player: Player;
var actors: Actor[..512];
var fireballs: Fireball[64];
var next_fireball = 0;
var game_time: f32 = 0.0;
var damage_clock: f32 = 0.0;
var kills = 0;
var total_monsters = 0;
var secrets = 0;
var total_secrets = 0;
var won = false;
var message: u8[..160];
var message_time: f32 = 0.0;
var random_state: u32 = 12345;

fn random_int(limit: i64) -> i64 {
    random_state = random_state * 1664525 + 1013904223;
    (random_state >> 8) % limit
}
fn say(text: const u8[:]) {
    message.resize(0);
    message.append(text);
    message_time = 3.5;
}
fn monster(kind: i64) -> bool { kind == 3004 || kind == 9 || kind == 3001 || kind == 3002 || kind == 58 }
fn monster_prefix(kind: i64) -> const u8[:] {
    match kind { 3004 => "POSS", 9 => "SPOS", 3001 => "TROO", _ => "SARG" }
}
fn monster_health(kind: i64) -> i64 {
    match kind { 3004 => 20, 9 => 30, 3001 => 60, _ => 150 }
}
fn thing_picture(kind: i64) -> const u8[:] {
    match kind {
        3004 => "POSSA1", 9 => "SPOSA1", 3001 => "TROOA1", 3002 => "SARGA1", 58 => "SARGA1",
        2001 => "SHOTA0", 2002 => "MGUNA0", 2003 => "LAUNA0", 2004 => "PLASA0",
        2005 => "CSAWA0", 2006 => "BFUGA0", 2007 => "CLIPA0", 2008 => "SHELA0",
        2010 => "ROCKA0", 2046 => "BROKA0", 2047 => "CELLA0", 2048 => "AMMOA0",
        2049 => "SBOXA0", 17 => "CELPA0", 8 => "BPAKA0",
        2011 => "STIMA0", 2012 => "MEDIA0", 2013 => "SOULA0", 2014 => "BON1A0",
        2015 => "BON2A0", 2018 => "ARM1A0", 2019 => "ARM2A0", 2023 => "PSTRA0",
        5 => "BKEYA0", 2035 => "BAR1A0", 2028 => "COLUA0",
        43 => "TRE1A0", 54 => "TRE2A0", 47 => "SMITA0", 48 => "ELECA0",
        10 => "PLAYW0", 12 => "PLAYW0", 15 => "PLAYN0", 18 => "POSSL0",
        19 => "SPOSL0", 20 => "TROOM0", 21 => "SARGN0", 24 => "POL5A0",
        26 => "POL6A0", 60 => "GOR4A0", _ => "",
    }
}

let SOUND_NAMES: const u8[:][12] = ["DSPISTOL", "DSSHOTGN", "DSPUNCH", "DSPLPAIN",
    "DSITEMUP", "DSWPNUP", "DSDOROPN", "DSPSTART", "DSSWTCHN", "DSFIRSHT", "DSBAREXP", "DSPODTH1"];
var sounds: audio::Sound[12];
var sound_ready = false;
fn load_sounds(quiet: bool, offline: bool = false) {
    if quiet || !audio::available() { return; }
    sound_ready = if offline { audio::open_offline() } else { audio::open() };
    if !sound_ready {
        write_stderr(str("audio unavailable; continuing silently: ", audio::error(), "\n"));
        return;
    }
    for i in SOUND_NAMES.len {
        let data = lump(SOUND_NAMES[i]);
        require(data.len >= 8 && u16le(data, 0) == 3, "expected DMX PCM sound");
        let rate = u16le(data, 2);
        let count = u32le(data, 4);
        require(rate >= 8000 && rate <= 192000 && count > 32 && binary::range_ok(data, 8, count), "bad DMX sound");
        var pcm: f32[>..] = [];
        // DMX adds sixteen padding samples at each end.
        for j in 16..count - 16 { pcm.push((data[8 + j] - 128.0) / 128.0); }
        sounds[i] = audio::create_sound(pcm, rate);
        require(sounds[i].id != 0, audio::error());
    }
}
fn sound(id: i64, pos: float2) {
    if !sound_ready { return; }
    let delta = pos - player.pos;
    let dist = length(delta);
    let right = float2 { sin(player.yaw), -cos(player.yaw) };
    let pan = clamp(dot(delta, right) / max(dist, 1.0), -1.0, 1.0);
    audio::play(sounds[id], clamp(1.0 - dist / 1400.0, 0.0, 0.7), pan);
}
fn reset_game() {
    actors.resize(0);
    for &f in fireballs { f.life = 0.0; }
    next_fireball = 0;
    kills = 0;
    secrets = 0;
    total_monsters = 0;
    total_secrets = 0;
    game_time = 0.0;
    damage_clock = 0.0;
    random_state = 12345;
    won = false;
    for &line in lines { line.activated = false; }
    for &s in sectors {
        s.floor = s.start_floor;
        s.ceiling = s.start_ceiling;
        s.motion = 0;
        s.wait = 0.0;
        s.secret_found = false;
        if s.special == 9 { total_secrets++; }
    }
    player = Player { health: 100, bullets: 50, weapon: 2, .. };
    let things = records("THINGS", 10);
    var have_start = false;
    for i in things.len / 10 {
        let at = i * 10;
        let pos = float2 { i16le(things, at), i16le(things, at + 2) };
        let kind = u16le(things, at + 6);
        let flags = u16le(things, at + 8);
        let z = sectors[sector_at(pos)].floor;
        if kind == 1 {
            player.pos = pos;
            player.z = z;
            player.yaw = i16le(things, at + 4) * PI / 180.0;
            have_start = true;
        }
        // Skill bit 2 is medium; bit 16 marks multiplayer-only things.
        if (flags & 2) == 0 || (flags & 16) != 0 { continue; }
        let name = thing_picture(kind);
        if name.len == 0 { continue; }
        let picture = find_picture(name, 0);
        require(picture >= 0, str("missing thing sprite ", name));
        let enemy = monster(kind);
        let solid = enemy || kind == 2035 || kind == 2028 || kind == 43 ||
                    kind == 54 || kind == 47 || kind == 48;
        require(actors.len < 512, "too many map things");
        actors.push(Actor { pos: pos, z: z, kind: kind, picture: picture,
            radius: if kind == 3002 || kind == 58 { 30.0 } else { 16.0 },
            health: if enemy { monster_health(kind) } else if kind == 2035 { 20 } else { 0 },
            alive: true, solid: solid, timer: 0.5 + i % 10 * 0.1, .. });
        if enemy { total_monsters++; }
    }
    require(have_start, "missing player-one start");
    if sound_ready { audio::stop_all(); }
    say("FIND THE BLUE KEY AND THE EXIT.  H: HELP");
}

// --- collision, sight and walking -------------------------------------------

fn nearest_on_line(p: float2, a: float2, b: float2) -> float2 {
    let d = b - a;
    let t = clamp(dot(p - a, d) / max(dot(d, d), 0.001), 0.0, 1.0);
    a + scale2(d, t)
}
// Intersection distance along a finite ray. -1 means no intersection.
fn intersect_ray(pos: float2, direction: float2, a: float2, b: float2) -> f32 {
    let edge = b - a;
    let denominator = cross2(direction, edge);
    if abs(denominator) < 0.00001 { return -1.0; }
    let t = cross2(a - pos, edge) / denominator;
    let u = cross2(a - pos, direction) / denominator;
    if t > 0.001 && u >= 0.0 && u <= 1.0 { t } else { -1.0 }
}
fn wall_distance(pos: float2, z: f32, direction: float2, slope: f32, limit: f32) -> f32 {
    var nearest = limit;
    for line in lines {
        let t = intersect_ray(pos, direction, line.a, line.b);
        if t < 0.0 || t >= nearest { continue; }
        let back = back_sector(line);
        if back < 0 { nearest = t; continue; }
        let a = sectors[front_sector(line)];
        let b = sectors[back];
        let height = z + t * slope;
        if height <= max(a.floor, b.floor) || height >= min(a.ceiling, b.ceiling) { nearest = t; }
    }
    nearest
}
fn visible(a: float2, az: f32, b: float2, bz: f32) -> bool {
    let dist = distance2(a, b);
    if dist < 0.1 { return true; }
    wall_distance(a, az, scale2(b - a, 1.0 / dist), (bz - az) / dist, dist) >= dist - 0.1
}
fn can_stand(pos: float2, z: f32, radius: f32, height: f32, ignore: i64, enemy: bool) -> bool {
    let here = sectors[sector_at(pos)];
    if here.floor > z + 24.0 || here.ceiling < max(z, here.floor) + height { return false; }
    for line in lines {
        // A tiny tolerance lets a circle slide along an exactly tangent
        // wall without floating-point drift turning contact into overlap.
        if distance2(pos, nearest_on_line(pos, line.a, line.b)) >= radius - 0.01 { continue; }
        let back = back_sector(line);
        if back < 0 || (line.flags & 1) != 0 || (enemy && (line.flags & 2) != 0) { return false; }
        let a = sectors[front_sector(line)];
        let b = sectors[back];
        let bottom = max(a.floor, b.floor);
        let top = min(a.ceiling, b.ceiling);
        if bottom > z + 24.0 || top - max(z, bottom) < height { return false; }
    }
    for i in actors.len {
        if i == ignore { continue; }
        let a = actors[i];
        if !a.alive || !a.solid { continue; }
        if abs(a.z - z) < height && distance2(pos, a.pos) < radius + a.radius { return false; }
    }
    if enemy && player.health > 0 && abs(player.z - z) < height &&
        distance2(pos, player.pos) < radius + PLAYER_RADIUS { return false; }
    true
}
fn slide(pos: float2, z: f32, delta: float2, radius: f32, ignore: i64, enemy: bool) -> float2 {
    var next = pos + delta;
    if can_stand(next, z, radius, PLAYER_HEIGHT, ignore, enemy) { return next; }
    // Axis fallback is small and forgiving. Substeps below prevent tunneling.
    next = float2 { pos.x + delta.x, pos.y };
    if !can_stand(next, z, radius, PLAYER_HEIGHT, ignore, enemy) { next = pos; }
    let y = float2 { next.x, pos.y + delta.y };
    if can_stand(y, z, radius, PLAYER_HEIGHT, ignore, enemy) { next = y; }
    next
}
fn walk(direction: float2, running: bool) {
    let forward = float2 { cos(player.yaw), sin(player.yaw) };
    let right = float2 { forward.y, -forward.x };
    var movement = scale2(forward, direction.y) + scale2(right, direction.x);
    if length(movement) > 1.0 { movement = normalize(movement); }
    movement = scale2(movement, (if running { 260.0 } else { 170.0 }) * STEP);
    let old = player.pos;
    let steps = max(1, ceil(length(movement) / 4.0) as i64);
    for i in steps {
        player.pos = slide(player.pos, player.z, scale2(movement, 1.0 / steps), PLAYER_RADIUS, -1, false);
        let floor_z = sectors[sector_at(player.pos)].floor;
        if floor_z > player.z { player.z = floor_z; player.falling = 0.0; }
    }
    let ground = sectors[sector_at(player.pos)].floor;
    if player.z > ground {
        player.falling -= 700.0 * STEP;
        player.z = max(ground, player.z + player.falling * STEP);
    } else { player.z = ground; player.falling = 0.0; }
    player.bob += distance2(old, player.pos) * 0.055;
    if distance2(old, player.pos) > 0.001 {
        for i in lines.len {
            let line = lines[i];
            if line.special != 2 && line.special != 88 { continue; }
            let t = intersect_ray(old, player.pos - old, line.a, line.b);
            if t > 0.0 && t <= 1.0 { activate_line(i, true); }
        }
    }
}

// --- moving sectors and use triggers ----------------------------------------

fn neighbor_limits(sector: i64) -> f32, f32 {
    var low = sectors[sector].start_floor;
    var top: f32 = 32768.0;
    for line in lines {
        let a = front_sector(line);
        let b = back_sector(line);
        var neighbor = -1;
        if a == sector { neighbor = b; }
        else if b == sector { neighbor = a; }
        if neighbor >= 0 && neighbor != sector {
            low = min(low, sectors[neighbor].floor);
            top = min(top, sectors[neighbor].ceiling);
        }
    }
    return low, top;
}
fn open_door(index: i64, fast: bool, stay: bool) {
    let s .= sectors[index];
    let low, top = neighbor_limits(index);
    if top == 32768.0 { return; }
    s.target = top - 4.0;
    s.motion = if stay { 4 } else { 1 };  // opening, waiting, closing, stay open
    s.speed = if fast { 280.0 } else { 90.0 };
    s.wait = 0.0;
}
fn activate_line(index: i64, crossing: bool) {
    let line = lines[index];
    let special = line.special;
    if crossing != (special == 2 || special == 88) { return; }
    if special == 11 {
        lines[index].activated = true;
        won = true;
        sound(8, player.pos);
        say("LEVEL COMPLETE!  R: PLAY AGAIN");
    } else if special == 1 || special == 26 || special == 117 {
        if special == 26 && !player.blue_key { say("YOU NEED THE BLUE KEY"); return; }
        let back = back_sector(line);
        if back >= 0 { open_door(back, special == 117, false); sound(6, player.pos); }
    } else if special == 2 || special == 62 || special == 88 || special == 23 {
        lines[index].activated = true;
        for i in sectors.len {
            if line.tag == 0 || sectors[i].tag != line.tag { continue; }
            if special == 2 { open_door(i, false, true); }
            else if sectors[i].motion == 0 {
                let low, top = neighbor_limits(i);
                sectors[i].target = low;
                sectors[i].speed = 120.0;
                sectors[i].motion = if special == 23 { 8 } else { 5 };
            }
        }
        sound(if special == 2 { 6 } else { 7 }, player.pos);
    }
}
fn use() {
    let direction = float2 { cos(player.yaw), sin(player.yaw) };
    var nearest: f32 = 80.0;
    var found = -1;
    for i in lines.len {
        let line = lines[i];
        let t = intersect_ray(player.pos, direction, line.a, line.b);
        if t < 0.0 || t >= nearest { continue; }
        let b = back_sector(line);
        // Usable faces and closed passages stop the use ray. A farther
        // switch must never be activated through a wall or a closed door.
        let blocks = b < 0 || (b >= 0 && sectors[b].ceiling <= sectors[b].floor);
        let usable = line.special != 0 && line.special != 2 && line.special != 88 &&
                     cross2(line.b - line.a, player.pos - line.a) <= 0.0;
        if blocks || usable { nearest = t; found = if usable { i } else { -1 }; }
    }
    if found >= 0 { activate_line(found, false); }
}
fn occupied(index: i64, ceiling: f32) -> bool {
    if sector_at(player.pos) == index && player.z + PLAYER_HEIGHT > ceiling { return true; }
    for a in actors {
        if a.alive && a.solid && sector_at(a.pos) == index && a.z + PLAYER_HEIGHT > ceiling { return true; }
    }
    false
}
fn move_sectors() {
    for i in sectors.len {
        let s .= sectors[i];
        if s.motion == 0 { continue; }
        let amount = s.speed * STEP;
        if s.motion == 1 || s.motion == 4 {
            s.ceiling = min(s.target, s.ceiling + amount);
            if s.ceiling == s.target {
                s.motion = if s.motion == 4 { 0 } else { 2 };
                s.wait = 4.0;
            }
        } else if s.motion == 2 {
            s.wait -= STEP;
            if s.wait <= 0.0 { s.motion = 3; }
        } else if s.motion == 3 {
            let next = max(s.floor, s.ceiling - amount);
            if occupied(i, next) { s.motion = 1; }
            else { s.ceiling = next; if next == s.floor { s.motion = 0; } }
        } else {
            let old_floor = s.floor;
            if s.motion == 5 || s.motion == 8 {
                s.floor = max(s.target, s.floor - amount);
                if s.floor == s.target {
                    s.motion = if s.motion == 8 { 0 } else { 6 };
                    s.wait = 3.0;
                }
            } else if s.motion == 6 {
                s.wait -= STEP;
                if s.wait <= 0.0 { s.motion = 7; }
            } else if s.motion == 7 {
                let next = min(s.start_floor, s.floor + amount);
                // A lift reverses instead of crushing someone into a ceiling.
                if occupied(i, s.ceiling - (next - s.floor)) { s.motion = 5; }
                else { s.floor = next; if next == s.start_floor { s.motion = 0; } }
            }
            if sector_at(player.pos) == i && abs(player.z - old_floor) < 0.1 { player.z = s.floor; }
            for &a in actors {
                if a.alive && sector_at(a.pos) == i && abs(a.z - old_floor) < 0.1 { a.z = s.floor; }
            }
        }
    }
}

// --- pickups and combat -----------------------------------------------------

fn hurt_player(damage: i64) {
    if player.health <= 0 || won { return; }
    let absorbed = min(player.armor, damage / 3);
    player.armor -= absorbed;
    player.health = max(0, player.health - damage + absorbed);
    player.hurt = 0.35;
    sound(3, player.pos);
    if player.health == 0 { say("YOU DIED.  R: TRY AGAIN"); }
}
fn pickup(index: i64) {
    let a .= actors[index];
    let kind = a.kind;
    var taken = false;
    var text: const u8[:] = "PICKED UP AMMO";
    if kind == 5 { player.blue_key = true; taken = true; text = "BLUE KEY"; }
    else if kind == 2001 {
        taken = !player.shotgun || player.shells < 100;
        if !player.shotgun { player.weapon = 3; }
        player.shotgun = true;
        player.shells = min(100, player.shells + 8);
        text = "SHOTGUN";
    } else if kind == 2002 {
        taken = !player.chaingun || player.bullets < 300;
        if !player.chaingun { player.weapon = 4; }
        player.chaingun = true;
        player.bullets = min(300, player.bullets + 20);
        text = "CHAINGUN";
    } else if kind == 2008 || kind == 2049 {
        taken = player.shells < 100;
        player.shells = min(100, player.shells + (if kind == 2049 { 20 } else { 4 }));
        text = "SHELLS";
    } else if kind == 2007 || kind == 2048 || kind == 8 || kind == 2010 || kind == 2046 ||
              kind == 2047 || kind == 17 || kind == 2003 || kind == 2004 || kind == 2005 {
        taken = player.bullets < 300;
        player.bullets = min(300, player.bullets + (if kind == 2048 || kind == 8 { 50 } else { 10 }));
        if kind == 8 { player.shells = min(100, player.shells + 8); }
        if kind != 2007 && kind != 2048 && kind != 8 { text = "SALVAGED BULLETS"; }
    } else if kind == 2011 || kind == 2012 || kind == 2013 || kind == 2014 || kind == 2023 {
        let maximum = if kind == 2013 || kind == 2014 { 200 } else { 100 };
        taken = player.health < maximum;
        let amount = match kind { 2014 => 1, 2011 => 10, 2012 => 25, _ => 100 };
        if taken { player.health = min(maximum, player.health + amount); }
        text = "HEALTH";
    } else if kind == 2015 || kind == 2018 || kind == 2019 {
        let amount = match kind { 2015 => 1, 2018 => 100, _ => 200 };
        taken = if kind == 2015 { player.armor < 200 } else { player.armor < amount };
        if taken { player.armor = if kind == 2015 { player.armor + 1 } else { amount }; }
        text = "ARMOR";
    }
    if taken {
        a.alive = false;
        player.bonus = 0.18;
        say(text);
        sound(if kind == 2001 || kind == 2002 { 5 } else { 4 }, player.pos);
    }
}
fn damage_actor(index: i64, damage: i64) {
    let a .= actors[index];
    if !a.alive || a.health <= 0 { return; }
    a.health -= damage;
    a.awake = true;
    a.pain = 0.18;
    if a.health > 0 { return; }
    a.solid = false;
    a.death = 0.001;
    if monster(a.kind) { kills++; sound(11, a.pos); }
    // Barrel explosions are processed by update_actors, avoiding recursive
    // chain reactions on the native call stack.
}
fn shoot() {
    if player.cooldown > 0.0 || player.health <= 0 { return; }
    let weapon = player.weapon;
    if (weapon == 3 && player.shells == 0) || ((weapon == 2 || weapon == 4) && player.bullets == 0) {
        player.weapon = 1;
        say("OUT OF AMMO - FISTS");
        return;
    }
    if weapon == 3 { player.shells--; }
    else if weapon != 1 { player.bullets--; }
    player.cooldown = match weapon { 1 => 0.42, 2 => 0.28, 3 => 0.85, _ => 0.10 };
    player.flash = 0.09;
    sound(if weapon == 1 { 2 } else if weapon == 3 { 1 } else { 0 }, player.pos);
    let pellets = if weapon == 3 { 7 } else { 1 };
    for pellet in pellets {
        let spread: f32 = if weapon == 3 { (random_int(101) - 50) * 0.0015 }
                         else if weapon == 4 { (random_int(101) - 50) * 0.0005 } else { 0.0 };
        let direction = float2 { cos(player.yaw + spread), sin(player.yaw + spread) };
        var nearest: f32 = if weapon == 1 { 70.0 } else { 1800.0 };
        var target = -1;
        for i in actors.len {
            let a = actors[i];
            if !a.alive || a.health <= 0 { continue; }
            let delta = a.pos - player.pos;
            let along = dot(delta, direction);
            let across = abs(cross2(direction, delta));
            if along <= 0.0 || along >= nearest || across > a.radius { continue; }
            // Classic horizontal aiming: a modest automatic vertical aim
            // helps with stairs, but a ray through the opening must be clear.
            if abs(a.z + 28.0 - (player.z + 41.0)) > 40.0 + along * 0.5 { continue; }
            if visible(player.pos, player.z + 41.0, a.pos, a.z + 28.0) {
                target = i;
                nearest = along;
            }
        }
        if target >= 0 {
            let damage = if weapon == 1 { 12 + random_int(12) } else { 5 * (1 + random_int(3)) };
            damage_actor(target, damage);
        }
    }
    // Noise wakes nearby monsters only through open sight lines.
    for &a in actors {
        if monster(a.kind) && distance2(a.pos, player.pos) < 1000.0 &&
            visible(a.pos, a.z + 32.0, player.pos, player.z + 32.0) { a.awake = true; }
    }
}
fn launch_fireball(a: Actor) {
    let delta = player.pos - a.pos;
    let dist = max(1.0, length(delta));
    fireballs[next_fireball] = Fireball { a.pos, a.z + 32.0, scale2(delta, 280.0 / dist),
        (player.z + 30.0 - a.z - 32.0) * 280.0 / dist, 5.0 };
    next_fireball = (next_fireball + 1) % fireballs.len;
    sound(9, a.pos);
}
fn update_actors() {
    for i in actors.len {
        let a .= actors[i];
        if !a.alive { continue; }
        if a.death > 0.0 {
            let old = a.death;
            a.death += STEP;
            if a.kind == 2035 && old < 0.15 && a.death >= 0.15 {
                sound(10, a.pos);
                let dist = distance2(a.pos, player.pos);
                if dist < 160.0 && visible(a.pos, a.z + 24.0, player.pos, player.z + 32.0) {
                    hurt_player(max(0, (120.0 - dist * 0.7) as! i64));
                }
                for j in actors.len {
                    let other = actors[j];
                    let d = distance2(a.pos, other.pos);
                    if j != i && other.health > 0 && d < 160.0 &&
                        visible(a.pos, a.z + 24.0, other.pos, other.z + 28.0) {
                        damage_actor(j, max(0, (120.0 - d * 0.7) as! i64));
                    }
                }
            }
            if a.kind == 2035 && a.death > 0.65 { a.alive = false; }
            // Guards leave a clip/shotgun after their corpse settles. The
            // dropped item is a separate actor, so the corpse remains visible.
            if old < 0.8 && a.death >= 0.8 && (a.kind == 3004 || a.kind == 9) && actors.len < 512 {
                let kind = if a.kind == 9 { 2001 } else { 2007 };
                actors.push(Actor { pos: a.pos, z: a.z, kind: kind,
                    picture: find_picture(thing_picture(kind), 0), alive: true, .. });
            }
            continue;
        }
        a.z = max(sectors[sector_at(a.pos)].floor, a.z - 220.0 * STEP);
        if !monster(a.kind) { continue; }
        a.pain = max(0.0, a.pain - STEP);
        a.attacking = max(0.0, a.attacking - STEP);
        a.timer -= STEP;
        let delta = player.pos - a.pos;
        let dist = length(delta);
        let sees = player.health > 0 && dist < 1500.0 && visible(a.pos, a.z + 36.0, player.pos, player.z + 32.0);
        if sees { a.awake = true; }
        if !a.awake || player.health <= 0 || a.pain > 0.0 { continue; }
        if sees && a.timer <= 0.0 && (dist < 650.0 || dist < a.radius + 38.0) {
            let melee = a.kind == 3002 || a.kind == 58;
            if !melee || dist < a.radius + 38.0 {
                a.attacking = 0.28;
                a.timer = 1.0 + random_int(100) * 0.012;
                if a.kind == 3001 && dist > 75.0 { launch_fireball(a); }
                else if melee || dist < 75.0 { hurt_player(5 + random_int(12)); }
                else {
                    sound(0, a.pos);
                    if random_int(100) < max(20, 85 - (dist * 0.08) as! i64) {
                        hurt_player(if a.kind == 9 { 6 + random_int(10) } else { 3 + random_int(7) });
                    }
                }
            }
        }
        if a.attacking > 0.0 || dist < 1.0 { continue; }
        let speed: f32 = if a.kind == 3002 || a.kind == 58 { 105.0 } else { 58.0 };
        let movement = scale2(delta, speed * STEP / dist);
        a.pos = slide(a.pos, a.z, movement, a.radius, i, true);
        a.z = max(a.z, sectors[sector_at(a.pos)].floor);
    }
    for &f in fireballs {
        if f.life <= 0.0 { continue; }
        f.life -= STEP;
        let next = f.pos + scale2(f.velocity, STEP);
        let next_z = f.z + f.vz * STEP;
        if !visible(f.pos, f.z, next, next_z) { f.life = 0.0; continue; }
        f.pos = next;
        f.z = next_z;
        if distance2(f.pos, player.pos) < 22.0 && f.z > player.z && f.z < player.z + PLAYER_HEIGHT {
            hurt_player(10 + random_int(15));
            f.life = 0.0;
        }
    }
}
fn tick(direction: float2, running: bool, firing: bool) {
    game_time += STEP;
    message_time = max(0.0, message_time - STEP);
    player.cooldown = max(0.0, player.cooldown - STEP);
    player.flash = max(0.0, player.flash - STEP);
    player.hurt = max(0.0, player.hurt - STEP);
    player.bonus = max(0.0, player.bonus - STEP);
    move_sectors();
    if player.health > 0 {
        walk(direction, running);
        if firing { shoot(); }
        let s .= sectors[sector_at(player.pos)];
        if s.special == 9 && !s.secret_found {
            s.secret_found = true;
            secrets++;
            say("A SECRET IS REVEALED");
        }
        damage_clock += STEP;
        if damage_clock >= 1.0 {
            damage_clock -= 1.0;
            if s.special == 7 && player.z <= s.floor + 1.0 { hurt_player(5); }
        }
        for i in actors.len {
            let a = actors[i];
            if a.alive && a.death == 0.0 && !a.solid && distance2(a.pos, player.pos) < 28.0 &&
                abs(a.z - player.z) < 32.0 { pickup(i); }
        }
    }
    update_actors();
}

// --- meshes: world units become (x, height, -y) -------------------------------

struct Vertex { pos: float3, uv: float2, rectangle: float4, light: f32 }
struct HudVertex { pos: float2, uv: float2, rectangle: float4, color: float4 }
// The roots the two pipelines' shaders read, declared again in them: a
// pointer to this frame's vertices, the atlas and its sampler by index, and
// the camera or the HUD's canvas size.
struct WorldRoot { vertices: u64, atlas: u32, sampler: u32, view_proj: ngfx::float4x4, eye: float4 }
struct HudRoot { vertices: u64, atlas: u32, sampler: u32, screen: float4 }

fn atlas_rect(id: i64) -> float4 {
    let p = pictures[id];
    float4 { p.x, p.y, p.w, p.h }
}
fn world_point(p: float2, z: f32) -> float3 { float3 { p.x, z, -p.y } }
fn lighting(sector: i64) -> f32 {
    let s = sectors[sector];
    if s.special == 1 && ((game_time * 9.0) as! i64 + sector * 7) % 11 < 3 { return s.light * 0.4; }
    if s.special == 12 && game_time % 1.0 < 0.15 { return s.light * 0.35; }
    max(0.18, s.light)
}
fn animated_flat(id: i64) -> i64 {
    if id < 0 { return sky_picture; }
    let name = pictures[id].name;
    if name == "NUKAGE1" || name == "NUKAGE2" || name == "NUKAGE3" {
        return find_picture(str("NUKAGE", 1 + (game_time * 4.0) as! i64 % 3), 2);
    }
    if name == "FWATER1" || name == "FWATER2" || name == "FWATER3" || name == "FWATER4" {
        return find_picture(str("FWATER", 1 + (game_time * 4.0) as! i64 % 4), 2);
    }
    id
}
fn switch_texture(id: i64, activated: bool) -> i64 {
    if id < 0 || !activated { return id; }
    let name = pictures[id].name;
    if name.len >= 3 && name[..3] == "SW1" {
        let on = find_picture(str("SW2", name[3..]), 1);
        if on >= 0 { return on; }
    }
    id
}
fn add_wall(out: Vertex[>..]&, a: float2, b: float2, bottom: f32, top: f32,
            texture: i64, offset: float2, anchor: f32, light: f32) {
    if texture < 0 || top <= bottom { return; }
    let pic = pictures[texture];
    let u0 = offset.x / pic.w;
    let u1 = (offset.x + distance2(a, b)) / pic.w;
    let v0 = (anchor - top + offset.y) / pic.h;
    let v1 = (anchor - bottom + offset.y) / pic.h;
    let rectangle = atlas_rect(texture);
    let corners: Vertex[4] = [
        Vertex { world_point(a, bottom), float2 { u0, v1 }, rectangle, light },
        Vertex { world_point(b, bottom), float2 { u1, v1 }, rectangle, light },
        Vertex { world_point(b, top), float2 { u1, v0 }, rectangle, light },
        Vertex { world_point(a, top), float2 { u0, v0 }, rectangle, light }];
    for i in [0, 1, 2, 0, 2, 3] { out.push(corners[i]); }
}
fn world_mesh(out: Vertex[>..]&) {
    for triangle in floor_triangles {
        let s = sectors[triangle.sector];
        for plane in 2 {
            let height = if plane == 0 { s.floor } else { s.ceiling };
            let original = if plane == 0 { s.floor_pic } else { s.ceiling_pic };
            let texture = animated_flat(original);
            let shade = if original < 0 { -1.0 } else { lighting(triangle.sector) };
            let rectangle = atlas_rect(texture);
            for p in [triangle.a, triangle.b, triangle.c] {
                out.push(Vertex { world_point(p, height), scale2(p, 1.0 / 64.0), rectangle, shade });
            }
        }
    }
    for line in lines {
        for face in 2 {
            let side_id = if face == 0 { line.front } else { line.back };
            if side_id < 0 { continue; }
            let other_id = if face == 0 { line.back } else { line.front };
            var side = sides[side_id];
            side.upper = switch_texture(side.upper, line.activated);
            side.lower = switch_texture(side.lower, line.activated);
            side.middle = switch_texture(side.middle, line.activated);
            let s = sectors[side.sector];
            let a = if face == 0 { line.a } else { line.b };
            let b = if face == 0 { line.b } else { line.a };
            // Only the side facing the viewer contributes. This also avoids
            // two coplanar middle textures fighting for the depth buffer.
            if cross2(b - a, player.pos - a) > 0.0 { continue; }
            let shade = lighting(side.sector) * (if abs(a.x - b.x) < 0.1 { 0.88 } else { 1.0 });
            if other_id < 0 {
                if side.middle < 0 { continue; }
                let anchor = if (line.flags & 16) != 0 { s.floor + pictures[side.middle].h } else { s.ceiling };
                add_wall(out, a, b, s.floor, s.ceiling, side.middle, side.offset, anchor, shade);
                continue;
            }
            let other = sectors[sides[other_id].sector];
            if side.upper >= 0 && !(s.ceiling_pic < 0 && other.ceiling_pic < 0) {
                let anchor = if (line.flags & 8) != 0 { s.ceiling }
                             else { other.ceiling + pictures[side.upper].h };
                add_wall(out, a, b, max(s.floor, other.ceiling), s.ceiling,
                         side.upper, side.offset, anchor, shade);
            }
            if side.lower >= 0 {
                let anchor = if (line.flags & 16) != 0 { s.ceiling } else { other.floor };
                add_wall(out, a, b, s.floor, min(s.ceiling, other.floor),
                         side.lower, side.offset, anchor, shade);
            }
            if side.middle >= 0 {
                let bottom = max(s.floor, other.floor);
                let top = min(s.ceiling, other.ceiling);
                let height: f32 = pictures[side.middle].h;
                let anchor = if (line.flags & 16) != 0 { bottom + height } else { top };
                add_wall(out, a, b, max(bottom, anchor + side.offset.y - height),
                         min(top, anchor + side.offset.y), side.middle, side.offset, anchor, shade);
            }
        }
    }
}
fn frame_picture(prefix: const u8[:], frame: i64) -> i64 {
    let letter: u8[1] = [('A' + frame) as u8];
    var id = find_picture(str(prefix, letter, "0"), 0);
    if id < 0 { id = find_picture(str(prefix, letter, "1"), 0); }
    require(id >= 0, str("missing sprite frame ", prefix, letter));
    id
}
fn actor_picture(a: Actor) -> i64 {
    if a.kind == 2035 {
        if a.death > 0.0 { return frame_picture("BEXP", min(4, (a.death * 8.0) as! i64)); }
        return frame_picture("BAR1", (game_time * 4.0) as! i64 % 2);
    }
    if !monster(a.kind) { return a.picture; }
    let prefix = monster_prefix(a.kind);
    if a.death > 0.0 {
        let first = if a.kind == 3001 || a.kind == 3002 || a.kind == 58 { 8 } else { 7 };
        let last = if a.kind == 3002 || a.kind == 58 { 5 } else { 4 };
        return frame_picture(prefix, first + min(last, (a.death * 7.0) as! i64));
    }
    if a.pain > 0.0 {
        return frame_picture(prefix, if a.kind == 3004 || a.kind == 9 { 6 } else { 7 });
    }
    if a.attacking > 0.0 { return frame_picture(prefix, 5); }
    frame_picture(prefix, if a.awake { (game_time * 8.0) as! i64 % 4 } else { 0 })
}
fn billboard(out: Vertex[>..]&, pos: float2, bottom: f32, id: i64, shade: f32) {
    let p = pictures[id];
    let right = float2 { sin(player.yaw), -cos(player.yaw) };
    // Use the WAD's anchor: different animation frames then stay registered.
    let a = pos - scale2(right, p.left);
    let b = a + scale2(right, p.w);
    let top = bottom + p.top;
    add_wall(out, a, b, top - p.h, top, id, float2_0, top, shade);
}
fn sprite_mesh(out: Vertex[>..]&) {
    for a in actors {
        if !a.alive { continue; }
        let shade = if a.kind == 2035 && a.death > 0.0 { 1.0 } else { lighting(sector_at(a.pos)) };
        billboard(out, a.pos, a.z, actor_picture(a), shade);
    }
    for f in fireballs {
        if f.life > 0.0 { billboard(out, f.pos, f.z - 8.0, frame_picture("BAL1", (game_time * 8.0) as! i64 % 2), 1.0); }
    }
}

// --- a small HUD, using the WAD's own font and weapon patches -----------------

let WHITE = float4 { 1.0, 1.0, 1.0, 1.0 };
fn hud_quad(out: HudVertex[>..]&, x: f32, y: f32, w: f32, h: f32, id: i64, color: float4) {
    let rectangle = atlas_rect(id);
    let points: float2[4] = [float2 { x, y }, float2 { x + w, y },
                            float2 { x + w, y + h }, float2 { x, y + h }];
    let uv: float2[4] = [float2 { 0.0, 0.0 }, float2 { 1.0, 0.0 },
                        float2 { 1.0, 1.0 }, float2 { 0.0, 1.0 }];
    for i in [0, 1, 2, 0, 2, 3] { out.push(HudVertex { points[i], uv[i], rectangle, color }); }
}
fn hud_picture(out: HudVertex[>..]&, x: f32, y: f32, scale: f32, id: i64) {
    let p = pictures[id];
    hud_quad(out, x, y, p.w * scale, p.h * scale, id, WHITE);
}
fn text_width(text: const u8[:]) -> f32 {
    var width: f32 = 0.0;
    for c in text { width += if c <= 32 || c >= 96 { 4.0 } else { pictures[font[c]].w + 1.0 }; }
    width
}
fn text(out: HudVertex[>..]&, x: f32, y: f32, words: const u8[:], color: float4) {
    var cursor = x;
    for c in words {
        if c <= 32 || c >= 96 { cursor += 4.0; continue; }
        let id = font[c];
        let p = pictures[id];
        hud_quad(out, cursor + 1.0, y + 1.0, p.w, p.h, id, float4 { 0.0, 0.0, 0.0, color.w });
        hud_quad(out, cursor, y, p.w, p.h, id, color);
        cursor += p.w + 1.0;
    }
}
fn centered(out: HudVertex[>..]&, width: f32, y: f32, words: const u8[:]) {
    text(out, (width - text_width(words)) * 0.5, y, words, WHITE);
}
fn map_line(out: HudVertex[>..]&, a: float2, b: float2, color: float4) {
    let d = b - a;
    let normal = scale2(float2 { -d.y, d.x }, 0.45 / max(0.1, length(d)));
    let rect = atlas_rect(0);
    let points: float2[4] = [a - normal, b - normal, b + normal, a + normal];
    for i in [0, 1, 2, 0, 2, 3] { out.push(HudVertex { points[i], float2_0, rect, color }); }
}
fn automap(out: HudVertex[>..]&, screen: float2) {
    hud_quad(out, 0.0, 0.0, screen.x, screen.y - 34.0, 0, float4 { 0.015, 0.025, 0.025, 0.94 });
    let center = float2 { screen.x * 0.5, (screen.y - 34.0) * 0.5 };
    for line in lines {
        let a = center + (line.a - player.pos) * float2 { 0.09, -0.09 };
        let b = center + (line.b - player.pos) * float2 { 0.09, -0.09 };
        let color = if line.special == 26 { float4 { 0.2, 0.45, 1.0, 1.0 } }
                    else if line.special != 0 { float4 { 1.0, 0.8, 0.25, 1.0 } }
                    else if line.back < 0 { float4 { 0.8, 0.23, 0.12, 1.0 } }
                    else { float4 { 0.3, 0.33, 0.3, 0.6 } };
        map_line(out, a, b, color);
    }
    let forward = float2 { cos(player.yaw), -sin(player.yaw) };
    let right = float2 { -forward.y, forward.x };
    let nose = center + scale2(forward, 6.0);
    map_line(out, center - scale2(right, 4.0), nose, WHITE);
    map_line(out, center + scale2(right, 4.0), nose, WHITE);
}
fn draw_hud(out: HudVertex[>..]&, screen: float2, paused: bool, help: bool, map: bool) {
    let bar_y = screen.y - 34.0;
    let center = screen.x * 0.5;
    if player.health > 0 && !map {
        let prefix = match player.weapon { 1 => "PUNG", 2 => "PISG", 3 => "SHTG", _ => "CHGG" };
        let frame = if player.weapon == 3 && player.cooldown > 0.12 {
            min(3, 1 + ((0.85 - player.cooldown) * 4.0) as! i64)
        } else if player.cooldown > 0.12 && player.weapon != 4 { 1 } else { 0 };
        let id = frame_picture(prefix, frame);
        let p = pictures[id];
        let bob = sin(player.bob) * 1.4;
        hud_picture(out, center - 160.0 - p.left + bob, bar_y - 168.0 - p.top + abs(bob), 1.0, id);
        if player.flash > 0.0 && player.weapon != 1 {
            let flash_id = frame_picture(if player.weapon == 3 { "SHTF" }
                                         else if player.weapon == 4 { "CHGF" } else { "PISF" }, 0);
            let f = pictures[flash_id];
            hud_picture(out, center - 160.0 - f.left + bob, bar_y - 168.0 - f.top + abs(bob), 1.0, flash_id);
        }
        hud_quad(out, center - 2.0, bar_y * 0.5, 5.0, 1.0, 0, float4 { 0.9, 0.9, 0.7, 0.7 });
        hud_quad(out, center, bar_y * 0.5 - 2.0, 1.0, 5.0, 0, float4 { 0.9, 0.9, 0.7, 0.7 });
    }
    if map { automap(out, screen); }
    if player.hurt > 0.0 {
        hud_quad(out, 0.0, 0.0, screen.x, bar_y, 0, float4 { 0.7, 0.0, 0.0, player.hurt * 0.7 });
    } else if player.bonus > 0.0 {
        hud_quad(out, 0.0, 0.0, screen.x, bar_y, 0, float4 { 1.0, 0.8, 0.2, player.bonus * 0.6 });
    }
    hud_quad(out, 0.0, bar_y, screen.x, 34.0, 0, float4 { 0.12, 0.12, 0.13, 1.0 });
    let face: u8[>..] = if player.health <= 0 { "STFDEAD0" }
        else { str("STFST", clamp((100 - player.health) / 20, 0, 4), "0") };
    hud_picture(out, center - 12.0, bar_y + 3.0, 1.0, find_picture(face, 0));
    text(out, 7.0, bar_y + 4.0, str("HEALTH ", player.health, "%"), WHITE);
    text(out, 7.0, bar_y + 18.0, str("ARMOR  ", player.armor, "%"), WHITE);
    let ammo: u8[>..] = if player.weapon == 1 { "FISTS" } else if player.weapon == 3 { str("SHELLS ", player.shells) }
               else { str("AMMO ", player.bullets) };
    text(out, center + 28.0, bar_y + 4.0, ammo, WHITE);
    text(out, center + 28.0, bar_y + 18.0, str("KILLS ", kills, "/", total_monsters), WHITE);
    if player.blue_key { hud_picture(out, screen.x - 14.0, bar_y + 3.0, 0.7, find_picture("BKEYA0", 0)); }
    if message_time > 0.0 { centered(out, screen.x, 7.0, message); }
    if help {
        hud_quad(out, 4.0, 22.0, 266.0, 56.0, 0, float4 { 0.0, 0.0, 0.0, 0.7 });
        text(out, 8.0, 26.0, "WASD: WALK  SHIFT: RUN  MOUSE: TURN", WHITE);
        text(out, 8.0, 38.0, "E/SPACE: USE  CLICK/CTRL: FIRE", WHITE);
        text(out, 8.0, 50.0, "1-4: WEAPON  TAB: MAP  H: HELP", WHITE);
        text(out, 8.0, 62.0, "ESC: PAUSE  F12: SCREENSHOT", WHITE);
    }
    if paused || player.health <= 0 || won {
        hud_quad(out, 0.0, bar_y * 0.5 - 22.0, screen.x, 48.0, 0, float4 { 0.0, 0.0, 0.0, 0.75 });
        centered(out, screen.x, bar_y * 0.5 - 15.0,
                 if won { "LEVEL COMPLETE" } else if player.health <= 0 { "YOU DIED" } else { "PAUSED" });
        centered(out, screen.x, bar_y * 0.5,
                 if won || player.health <= 0 { "R: RESTART" } else { "ESC OR CLICK TO RESUME" });
        if won { centered(out, screen.x, bar_y * 0.5 + 12.0, str("SECRETS ", secrets, "/", total_secrets)); }
    }
}

// --- GPU programs -----------------------------------------------------------

// What both modules share: the vertex layouts and the atlas lookup. A module
// has one root (GPU_ROOT), so the world and the HUD are a module each, this
// part embedded in both.
let shader_common = """
    #include <NoGraphicsAPI/shader.slang>

    struct Vertex { float3 pos; float2 uv; float4 rectangle; float light; };
    struct HudVertex { float2 pos; float2 uv; float4 rectangle; float4 color; };

    float4 sample_atlas(uint atlas, uint sampler, float2 texel) {
        Texture2D<float4> texture = gpu_texture<Texture2D<float4>>(atlas);
        return texture.Sample(gpu_sampler(sampler), texel / float2(2048.0, 4096.0));
    }
    """;
let world_shader = """
    struct WorldRoot { Vertex* vertices; uint atlas; uint sampler; float4x4 view_proj; float4 eye; };
    GPU_ROOT(WorldRoot, root);

    struct Varyings {
        float4 position : SV_Position;
        float2 uv : TEXCOORD0;
        nointerpolation float4 rectangle : TEXCOORD1;
        nointerpolation float light : TEXCOORD2;
        float3 direction : TEXCOORD3;
    };

    [shader("vertex")]
    Varyings worldVertex(uint vertex_id : SV_VertexID) {
        Vertex v = root.vertices[vertex_id];
        Varyings o;
        o.uv = v.uv;
        o.rectangle = v.rectangle;
        o.light = v.light;
        o.direction = v.pos - root.eye.xyz;
        o.position = mul(root.view_proj, float4(v.pos, 1.0));
        return o;
    }

    [shader("fragment")]
    float4 worldFragment(Varyings i) : SV_Target0 {
        float2 local = frac(i.uv);
        float shade = i.light;
        if (i.light < 0.0) {
            float angle = atan2(-i.direction.z, i.direction.x);
            local.x = frac(-angle * 2.0 / 3.14159265);
            local.y = clamp(0.55 - atan2(i.direction.y, length(i.direction.xz)) * 0.8, 0.0, 0.99);
            shade = 1.0;
        } else {
            shade *= max(0.38, 1.0 - length(i.direction) / 1800.0);
            shade = floor(shade * 24.0) / 24.0;
        }
        float2 texel = i.rectangle.xy + floor(local * i.rectangle.zw) + 0.5;
        float4 sample_color = sample_atlas(root.atlas, root.sampler, texel);
        if (sample_color.a < 0.5) discard;
        return float4(sample_color.rgb * shade, 1.0);
    }
    """;
// The HUD is in canvas units with y down, which NoGraphicsAPI's clip space
// also has.
let hud_shader = """
    struct HudRoot { HudVertex* vertices; uint atlas; uint sampler; float4 screen; };
    GPU_ROOT(HudRoot, root);

    struct Varyings {
        float4 position : SV_Position;
        float2 uv : TEXCOORD0;
        nointerpolation float4 rectangle : TEXCOORD1;
        float4 color : TEXCOORD2;
    };

    [shader("vertex")]
    Varyings hudVertex(uint vertex_id : SV_VertexID) {
        HudVertex v = root.vertices[vertex_id];
        Varyings o;
        o.uv = v.uv;
        o.rectangle = v.rectangle;
        o.color = v.color;
        o.position = float4(v.pos.x / root.screen.x * 2.0 - 1.0, v.pos.y / root.screen.y * 2.0 - 1.0, 0.0, 1.0);
        return o;
    }

    [shader("fragment")]
    float4 hudFragment(Varyings i) : SV_Target0 {
        float2 texel = i.rectangle.xy + clamp(floor(i.uv * i.rectangle.zw), float2(0.0), i.rectangle.zw - 1.0) + 0.5;
        return sample_atlas(root.atlas, root.sampler, texel) * i.color;
    }
    """;

// --- optional checks, run only with --test ----------------------------------

fn test_route(start: float2, goal: float2, key: bool) {
    reset_game();
    actors.resize(0);
    // First plan a route on a 16-unit grid. Doors may be opened, and lifts
    // connect their low and high floors. Then restore the map and follow the
    // route using the actual walking/use/tick code. Enemies are tested
    // separately: this check is specifically about traversing the geometry.
    for line in lines {
        if line.special == 1 || line.special == 117 || (key && line.special == 26) {
            open_door(back_sector(line), true, true);
        }
        if line.special == 2 {
            for i in sectors.len { if sectors[i].tag == line.tag { open_door(i, true, true); } }
        }
    }
    for i in 240 { move_sectors(); }
    for i in sectors.len {
        if sectors[i].tag == 1 || sectors[i].tag == 2 {
            let low, top = neighbor_limits(i);
            sectors[i].floor = low;
        }
    }
    // These bounds enclose the supplied E1M1; they are test data, not a
    // second representation of the level used by the game.
    let width = 256;
    let origin = float2 { -768.0, -1120.0 };
    var parent: i64[57344] = [-1; 57344];
    var queue: i64[>..] = [];
    let first = ((start.y - origin.y) / 16.0) as! i64 * width + ((start.x - origin.x) / 16.0) as! i64;
    queue.push(first);
    parent[first] = first;
    var head = 0;
    var found = -1;
    while head < queue.len {
        let cell = queue[head];
        head++;
        let pos = origin + float2 { (cell % width) * 16.0, (cell / width) * 16.0 };
        if distance2(pos, goal) < 24.0 { found = cell; break; }
        let here = sectors[sector_at(pos)];
        let z = if here.tag == 1 || here.tag == 2 { here.start_floor } else { here.floor };
        for offset in [-1, 1, -256, 256] {
            let next = cell + offset;
            if next < 0 || next >= parent.len || parent[next] >= 0 { continue; }
            let next_pos = origin + float2 { (next % width) * 16.0, (next / width) * 16.0 };
            if distance2(pos, next_pos) > 17.0 { continue; }
            if can_stand(next_pos, z, 16.0, 56.0, -1, false) {
                parent[next] = cell;
                queue.push(next);
            }
        }
    }
    require(found >= 0, "no route to key/exit");
    var path: float2[>..] = [];
    var cell = found;
    while cell != first {
        path.push(origin + float2 { (cell % width) * 16.0, (cell / width) * 16.0 });
        cell = parent[cell];
    }
    reset_game();
    actors.resize(0);
    player.pos = start;
    player.z = sectors[sector_at(start)].floor;
    player.blue_key = key;
    var waypoint = path.len;
    var ticks = 0;
    while waypoint > 0 && ticks < 12000 {
        let delta = path[waypoint - 1] - player.pos;
        if length(delta) < 3.0 { waypoint--; continue; }
        player.yaw = atan2(delta.y, delta.x);
        if ticks % 30 == 0 { use(); }
        tick(float2 { 0.0, 1.0 }, false, false);
        player.health = 100; // Geometry test: ignore damaging floors.
        ticks++;
    }
    require(waypoint == 0, "walking to key/exit got stuck");
}

fn gameplay_tests() {
    // These checks use the actual map, plus a few actors placed in its start
    // room. They need no GPU or sound device and leave a fresh game behind.
    require(sectors.len == 182 && lines.len == 1175, "unexpected E1M1 geometry");
    require(total_monsters > 20 && floor_triangles.len > 1000, "map did not load fully");
    require(can_stand(player.pos, player.z, PLAYER_RADIUS, PLAYER_HEIGHT, -1, false), "player start is blocked");
    let bytes: u8[4] = [255, 255, 0, 128];
    require(i16le(bytes, 0) == -1 && i16le(bytes, 2) == -32768 && u32le(bytes, 0) == 2147549183,
            "little-endian decoding failed");
    for triangle in floor_triangles {
        let center = scale2(triangle.a + triangle.b + triangle.c, 1.0 / 3.0);
        require(sector_at(center) == triangle.sector, "floor triangle escaped its BSP cell");
    }
    for line in lines {
        require(line.special == 0 || line.special == 1 || line.special == 2 || line.special == 11 ||
                line.special == 23 || line.special == 26 || line.special == 62 || line.special == 88 ||
                line.special == 117, "unimplemented special in the supplied map");
    }
    // Resolve every state now, including states absent from a still screenshot.
    for a in actors {
        var pose = a;
        actor_picture(pose);
        pose.awake = true;
        pose.pain = 0.1;
        actor_picture(pose);
        pose.pain = 0.0;
        pose.attacking = 0.1;
        actor_picture(pose);
        for frame in 6 {
            pose.death = 0.001 + frame / 7.0;
            actor_picture(pose);
        }
    }
    actors.resize(0);
    let start = player.pos;
    for i in 30 { walk(float2 { 0.0, 1.0 }, false); }
    require(distance2(start, player.pos) > 40.0, "player cannot leave the start");

    // A one-sided wall blocks both a moving circle and a shot.
    let wall = lines[0];
    let middle = scale2(wall.a + wall.b, 0.5);
    let edge = normalize(wall.b - wall.a);
    let normal = float2 { edge.y, -edge.x };
    let wall_z = sectors[front_sector(wall)].floor;
    require(!can_stand(middle, wall_z, 16.0, 56.0, -1, false), "solid wall permits standing");
    let near_wall = middle + scale2(normal, 32.0);
    let ray_length = wall_distance(near_wall, wall_z + 32.0, scale2(normal, -1.0), 0.0, 100.0);
    require(ray_length < 33.0, "solid wall permits shooting through it");
    player.pos = near_wall;
    player.z = wall_z;
    player.yaw = atan2(-normal.y, -normal.x);
    actors.push(Actor { pos: middle - scale2(normal, 32.0), z: wall_z,
        kind: 3001, radius: 16.0, health: 60, alive: true, solid: true, .. });
    shoot();
    require(actors[0].health == 60, "shot damaged a target behind a wall");
    actors.resize(0);

    // Real locked door: failed use, key pickup, opening, walking through,
    // and reopening when it tries to close on the player.
    var locked = -1;
    for i in lines.len { if lines[i].special == 26 { locked = i; break; } }
    require(locked >= 0, "missing blue door");
    let door_line = lines[locked];
    let door_sector = back_sector(door_line);
    let door_center = scale2(door_line.a + door_line.b, 0.5);
    let door_edge = normalize(door_line.b - door_line.a);
    let door_normal = float2 { door_edge.y, -door_edge.x };
    player.pos = door_center + scale2(door_normal, 40.0);
    player.z = sectors[sector_at(player.pos)].floor;
    player.yaw = atan2(-door_normal.y, -door_normal.x);
    use();
    require(sectors[door_sector].motion == 0, "blue door opened without key");
    actors.push(Actor { kind: 5, alive: true, .. });
    pickup(0);
    require(player.blue_key && !actors[0].alive, "key pickup failed");
    use();
    require(sectors[door_sector].motion == 1, "use did not open blue door");
    for i in 150 { move_sectors(); }
    require(sectors[door_sector].ceiling - sectors[door_sector].floor >= PLAYER_HEIGHT,
            "door did not open high enough");
    for i in 30 { walk(float2 { 0.0, 1.0 }, false); }
    require(dot(player.pos - door_center, door_normal) < -20.0, "player cannot cross an open door");
    player.pos = door_center - scale2(door_normal, 8.0);
    player.z = sectors[door_sector].floor;
    sectors[door_sector].motion = 3;
    for i in 150 { move_sectors(); }
    require(sectors[door_sector].ceiling >= player.z + PLAYER_HEIGHT, "door crushed the player");

    reset_game();
    actors.resize(0);
    var lift_line = -1;
    var lift = -1;
    for i in lines.len { if lines[i].special == 62 { lift_line = i; break; } }
    for i in sectors.len { if sectors[i].tag == lines[lift_line].tag { lift = i; break; } }
    require(lift >= 0, "missing tagged lift");
    activate_line(lift_line, false);
    for i in 180 { move_sectors(); }
    require(sectors[lift].floor < sectors[lift].start_floor, "lift did not lower");
    // Choose a triangle center in this lift to test carrying its rider.
    for triangle in floor_triangles {
        if triangle.sector == lift {
            player.pos = scale2(triangle.a + triangle.b + triangle.c, 1.0 / 3.0);
            break;
        }
    }
    player.z = sectors[lift].floor;
    for i in 360 { move_sectors(); }
    require(sectors[lift].floor == sectors[lift].start_floor && abs(player.z - sectors[lift].floor) < 0.1,
            "lift failed to return with its rider");

    // A target directly ahead in the start room. Pistol ammo is consumed,
    // the enemy dies, and its dropped ammunition can be collected.
    reset_game();
    actors.resize(0);
    actors.push(Actor { pos: player.pos + float2 { 72.0, 0.0 }, z: player.z,
        kind: 3004, picture: find_picture("POSSA1", 0), radius: 16.0, health: 20,
        alive: true, solid: true, .. });
    for i in 5 { player.cooldown = 0.0; shoot(); }
    require(kills == 1 && player.bullets == 45 && !actors[0].solid, "pistol combat failed");
    for i in 60 { update_actors(); }
    require(actors.len == 2 && actors[1].kind == 2007, "guard did not drop ammo");
    pickup(1);
    require(player.bullets == 55, "ammo pickup failed");
    actors.push(Actor { kind: 2001, alive: true, .. });
    pickup(2);
    require(player.shotgun && player.weapon == 3 && player.shells == 8, "shotgun pickup failed");
    actors.push(Actor { kind: 2012, alive: true, .. });
    pickup(3);
    require(actors[3].alive, "full-health player wasted a medkit");
    player.health = 50;
    pickup(3);
    require(player.health == 75 && !actors[3].alive, "medkit pickup failed");
    player.armor = 100;
    hurt_player(30);
    require(player.health == 55 && player.armor == 90, "armor absorption failed");

    reset_game();
    actors.resize(0);
    launch_fireball(Actor { pos: player.pos + float2 { 80.0, 0.0 }, z: player.z, .. });
    for i in 30 { update_actors(); }
    require(player.health < 100 && fireballs[0].life <= 0.0, "imp fireball failed to hit");
    fireballs[0] = Fireball { near_wall, wall_z + 32.0, scale2(normal, -280.0), 0.0, 2.0 };
    for i in 30 { update_actors(); }
    require(fireballs[0].life <= 0.0, "fireball passed through a solid wall");

    // Barrels trigger their neighbors without recursion. Corpses stop
    // blocking movement and the explosion effect eventually disappears.
    actors.resize(0);
    for i in 2 {
        actors.push(Actor { pos: player.pos + float2 { 72.0 + i * 32.0, 0.0 }, z: player.z,
            kind: 2035, radius: 16.0, health: 20, alive: true, solid: true, .. });
    }
    damage_actor(0, 100);
    for i in 60 { update_actors(); }
    require(!actors[0].alive && !actors[1].alive, "barrel chain reaction failed");

    // Run the real enemy update for ten seconds; resolve all live meshes.
    reset_game();
    for i in 600 { tick(float2_0, false, false); }
    var mesh: Vertex[>..] = [];
    world_mesh(mesh);
    sprite_mesh(mesh);
    require(mesh.len > 8000, "world mesh is incomplete");
    reset_game();
    var exit_line = -1;
    for i in lines.len { if lines[i].special == 11 { exit_line = i; break; } }
    let start_pos = player.pos;
    var key_pos = float2_0;
    for a in actors { if a.kind == 5 { key_pos = a.pos; } }
    test_route(start_pos, key_pos, false);
    let exit = lines[exit_line];
    let edge_to_exit = normalize(exit.b - exit.a);
    let exit_middle = scale2(exit.a + exit.b, 0.5);
    let exit_goal = exit_middle + scale2(float2 { edge_to_exit.y, -edge_to_exit.x }, 40.0);
    test_route(key_pos, exit_goal, true);
    let direction_to_exit = exit_middle - player.pos;
    player.yaw = atan2(direction_to_exit.y, direction_to_exit.x);
    use();
    require(won, "exit switch did not complete the level");
    reset_game();
    require(!won && kills == 0 && player.health == 100 && !player.blue_key, "restart left old state");
    if audio::available() {
        load_sounds(false, true);
        require(sound_ready, "offline audio mixer failed");
        sound(0, player.pos);
        var pcm: f32[4800] = [0.0; 4800];
        require(audio::render(pcm), audio::error());
        var audible = false;
        for sample in pcm { if abs(sample) > 0.001 { audible = true; } }
        require(audible, "WAD pistol sound decoded to silence");
        audio::close();
        sound_ready = false;
    }
    print("mini doom: WAD, BSP, collision, doors, lifts, combat, pickups, audio and key-to-exit routes passed");
}

// --- entry point: load once, then update and draw ----------------------------

fn main() {
    var frames = -1;
    var testing = false;
    var quiet = false;
    var path: u8[>..] = [];
    let argv = args();
    var argument = 1;
    while argument < argv.len {
        let option = argv[argument];
        if option == "--test" { testing = true; }
        else if option == "--silent" { quiet = true; }
        else if option == "--frames" {
            argument++;
            require(argument < argv.len, "--frames needs a positive count");
            let n, ok = parse_int(argv[argument]);
            require(ok && n > 0, "--frames needs a positive count");
            frames = n;
        } else if option == "--wad" {
            argument++;
            require(argument < argv.len && path.len == 0, "--wad needs one path");
            path.append(argv[argument]);
        } else { abort(str("unknown option: ", option)); }
        argument++;
    }
    if path.len == 0 {
        path.append(if file_exists("samples/data/freedoom1-e1m1.wad") {
            "samples/data/freedoom1-e1m1.wad"
        } else { "data/freedoom1-e1m1.wad" });
    }
    load_wad(path);
    load_pictures();
    load_map();
    reset_game();
    if testing { gameplay_tests(); reset_game(); }
    if testing && frames < 0 { return; }

    require(ngfx::open("Goose: Mini Doom / Freedoom E1M1", 960, 600, ngfx::WINDOW_RESIZABLE), ngfx::error());
    load_sounds(quiet || frames > 0);
    let atlas = ngfx::texture2d(ATLAS_W, ATLAS_H);
    require(atlas.id != 0 && ngfx::update_texture(atlas, atlas_pixels), ngfx::error());
    let sampler = ngfx::sampler(ngfx::NEAREST, ngfx::CLAMP);
    let world_pipeline = ngfx::pipeline(embed_slang(shader_common, world_shader),
        ngfx::PipelineDesc { depth_test: true, depth_write: true });
    let hud_pipeline = ngfx::pipeline(embed_slang(shader_common, hud_shader),
        ngfx::PipelineDesc { blend: ngfx::BLEND_ALPHA });
    require(sampler.id != 0 && world_pipeline.id != 0 && hud_pipeline.id != 0, ngfx::error());
    var paused = !ngfx::focused();
    var help = false;
    var map = false;
    var wait_for_fire = false;
    var fire_pending = false;
    var accumulator: f32 = 0.0;
    var drawn = 0;
    if !paused && frames < 0 && !ngfx::set_mouse_relative(true) { say("MOUSE CAPTURE FAILED - LEFT/RIGHT TURNS"); }
    while ngfx::frame() {
        let size = ngfx::screen_size();
        if size.x <= 0 || size.y <= 0 { continue; }
        let was_playing = !paused && player.health > 0 && !won;
        var lost_focus = !ngfx::focused();
        for event in ngfx::events() { if event.kind == ngfx::EVENT_FOCUS_LOST { lost_focus = true; } }
        if lost_focus { paused = true; }
        else {
            if ngfx::key_pressed("Escape") { paused = !paused; }
            else if paused && ngfx::mouse_pressed(ngfx::MOUSE_LEFT) { paused = false; }
            if ngfx::key_pressed("H") { help = !help; }
            if ngfx::key_pressed("Tab") { map = !map; }
            if ngfx::key_pressed("R") && (player.health <= 0 || won) { reset_game(); paused = false; }
        }
        let playing = !paused && player.health > 0 && !won;
        if playing != was_playing {
            ngfx::set_mouse_relative(playing && frames < 0);
            wait_for_fire = true;
        }
        let fire_down = ngfx::mouse_down(ngfx::MOUSE_LEFT) || ngfx::key_down("Left Ctrl") || ngfx::key_down("Right Ctrl");
        if !fire_down { wait_for_fire = false; }
        let dt: f32 = if frames > 0 { STEP } else { min(ngfx::delta_time(), 0.1) as f32 };
        if playing && was_playing {
            // Keep a short press until a simulation tick consumes it; a
            // press and release can both arrive between rendered frames.
            if !wait_for_fire && (ngfx::mouse_pressed(ngfx::MOUSE_LEFT) ||
                ngfx::key_pressed("Left Ctrl") || ngfx::key_pressed("Right Ctrl")) { fire_pending = true; }
            if ngfx::mouse_relative() { player.yaw -= ngfx::mouse_delta().x * 0.003; }
            if ngfx::key_down("Left") { player.yaw += dt * 2.2; }
            if ngfx::key_down("Right") { player.yaw -= dt * 2.2; }
            player.yaw = (player.yaw + TAU) % TAU;
            if ngfx::key_pressed("1") { player.weapon = 1; }
            if ngfx::key_pressed("2") { player.weapon = 2; }
            if ngfx::key_pressed("3") && player.shotgun { player.weapon = 3; }
            if ngfx::key_pressed("4") && player.chaingun { player.weapon = 4; }
            if ngfx::key_pressed("E") || ngfx::key_pressed("Space") { use(); }
            var direction = float2_0;
            if ngfx::key_down("W") || ngfx::key_down("Up") { direction.y += 1.0; }
            if ngfx::key_down("S") || ngfx::key_down("Down") { direction.y -= 1.0; }
            if ngfx::key_down("D") { direction.x += 1.0; }
            if ngfx::key_down("A") { direction.x -= 1.0; }
            accumulator += dt;
            while accumulator >= STEP && !won && player.health > 0 {
                tick(direction, ngfx::key_down("Left Shift") || ngfx::key_down("Right Shift"),
                     (fire_down || fire_pending) && !wait_for_fire);
                fire_pending = false;
                accumulator -= STEP;
            }
        } else { accumulator = 0.0; fire_pending = false; }
        // Death/completion can happen inside tick, after input was sampled.
        if (player.health <= 0 || won) && ngfx::mouse_relative() { ngfx::set_mouse_relative(false); }

        // All scratch mesh storage is freed when this frame's scope ends.
        // Rebuilding this small map keeps moving-sector rendering readable.
        var mesh: Vertex[>..] = [];
        world_mesh(mesh);
        sprite_mesh(mesh);
        let world_vertices = ngfx::frame_data(bytes_of(mesh));
        let hud_scale = min<f32>(size.x / 320.0, size.y / 200.0);
        let screen = float2 { size.x / hud_scale, size.y / hud_scale };
        var hud: HudVertex[>..] = [];
        draw_hud(hud, screen, paused, help, map);
        let hud_vertices = ngfx::frame_data(bytes_of(hud));
        let eye_height: f32 = if player.health <= 0 { 12.0 } else { 41.0 + sin(player.bob * 2.0) * 0.6 };
        let eye = world_point(player.pos, player.z + eye_height);
        let ahead = float3 { cos(player.yaw), 0.0, -sin(player.yaw) };
        let view = ngfx::look_at(eye, eye + ahead, float3 { 0.0, 1.0, 0.0 });
        let view_height: f32 = size.y * (screen.y - 34.0) / screen.y;
        let world_root = ngfx::root(WorldRoot {
            vertices: world_vertices, atlas: ngfx::index(atlas), sampler: ngfx::index(sampler),
            view_proj: ngfx::mul(ngfx::perspective(1.1, size.x / view_height, 1.0, 12000.0), view),
            eye: float4 { eye.x, eye.y, eye.z, 0.0 } });
        let hud_root = ngfx::root(HudRoot {
            vertices: hud_vertices, atlas: ngfx::index(atlas), sampler: ngfx::index(sampler),
            screen: float4 { screen.x, screen.y, 0.0, 0.0 } });
        require(ngfx::begin_screen(float4 { 0.16, 0.19, 0.22, 1.0 }), ngfx::error());
        ngfx::viewport(0.0, 0.0, size.x, view_height);
        ngfx::bind(world_pipeline);
        ngfx::draw(world_root, mesh.len);
        ngfx::viewport(0.0, 0.0, size.x, size.y);
        ngfx::bind(hud_pipeline);
        ngfx::draw(hud_root, hud.len);
        ngfx::end_pass();
        if ngfx::key_pressed("F12") {
            say(if ngfx::screenshot("ngfx_mini_doom.png") { "SAVED NGFX_MINI_DOOM.PNG" } else { "SCREENSHOT FAILED" });
        }
        drawn++;
        if drawn == frames { break; }
    }
    if frames > 0 {
        let saved = ngfx::screenshot("ngfx_mini_doom.png");
        require(saved, ngfx::error());
        print("drew ", drawn, " frames; saved ngfx_mini_doom.png: ", saved);
    }
    if sound_ready { audio::close(); }
    ngfx::close();
}
