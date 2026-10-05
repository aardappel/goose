// A little block world, built entirely from code: terrain, caves, trees,
// pixel textures, sky, and even the tiny font. There are no asset files.
// Start empty-handed, gather wood, make a pick, mine stone and coal, then
// build a cabin and light it. This is a programming sample, not an engine:
// a finite grid, a few plain structs, and one update/draw loop.
//
//   goose samples/30_mini_minecraft.goose
//   goose samples/30_mini_minecraft.goose -- --seed 123
//   goose samples/30_mini_minecraft.goose -- --frames 120
//   goose samples/30_mini_minecraft.goose -- --test
//
// WASD walk; Space jump/swim up; Shift run; mouse or arrows look.
// Ctrl sneaks slowly and stops at ledges; jumping releases that protection.
// Hold left mouse to mine; right mouse or E places (hold to build a row).
// 1-8 or wheel selects a slot; middle click picks the aimed-at material.
// C opens crafting; hold Shift with a recipe to make all affordable batches.
// H toggles help. Menus release the mouse cursor.
// Escape pauses/resumes; F5 saves; F9 loads; F12 takes a screenshot.
// Losing focus pauses. Click or Escape resumes when you return.
// Close the window to quit. Saving is explicit, to mini_minecraft.sav in
// the working directory; starting a new game never overwrites a save.
// --frames runs a fresh, deterministic world and saves mini_minecraft.png.
// --test checks gameplay without opening a window or touching save files.
//
// Deliberate limits: 80 x 40 x 80 blocks, no mobs, hunger, damage, dropped
// items, fluid simulation, tool wear, or infinite streaming. Water stays
// where it was generated. Materials go straight into the inventory.
// Lamps are full blocks, with at most sixteen placed lights. Lighting is
// approximate, so it can leak through a wall. Tools are permanent upgrades.
// A day lasts ten minutes; night is peaceful, but lamps help you see.
//
// Collision with this grid is simpler to show directly than to maintain
// a separate physics world full of static boxes.

import std;
import vec;
import math;
import os;
import gfx;

// --- blocks and game state ----------------------------------------------------

let WIDTH = 80;
let HEIGHT = 40;
let DEPTH = 80;
let VOLUME = WIDTH * HEIGHT * DEPTH;
let CHUNK = 16;
let CHUNKS_X = WIDTH / CHUNK;
let CHUNKS_Z = DEPTH / CHUNK;
let SEA = 12;
let SLOTS = 8;
let MAX_LAMPS = 16;
let DAY_LENGTH: f32 = 600.0;

let AIR: u8 = 0;
let GRASS: u8 = 1;
let DIRT: u8 = 2;
let STONE: u8 = 3;
let SAND: u8 = 4;
let WOOD: u8 = 5;
let LEAVES: u8 = 6;
let PLANKS: u8 = 7;
let COBBLE: u8 = 8;
let COAL: u8 = 9;
let WATER: u8 = 10;
let BEDROCK: u8 = 11;
let LAMP: u8 = 12;

// Inventory slots are separate from block IDs: stone drops cobblestone,
// grass drops dirt, and coal is a crafting ingredient rather than a block.
let SLOT_BLOCK: u8[SLOTS] = [DIRT, COBBLE, WOOD, PLANKS, LEAVES, SAND, LAMP, AIR];
let SLOT_NAME: const u8[:][SLOTS] =
    ["DIRT", "STONE", "WOOD", "PLANKS", "LEAVES", "SAND", "LAMPS", "COAL"];

struct Cell { x: i64, y: i64, z: i64 }
struct Player {
    feet: float3,
    velocity: f32,
    yaw: f32,
    pitch: f32,
    grounded: bool,
}

// This small, pointer-free record is the save header. The large block grid
// stays in global storage: passing or copying Game never copies the world.
// Transient input and GPU handles are kept outside the saved state.
struct Game {
    version: u32,
    seed: u32,
    player: Player,
    bag: i32[SLOTS],
    tool: i32,                 // 0: hands, 1: wooden pick, 2: stone pick
    selected: i32,
}
var game: Game;
var blocks: u8[VOLUME];
var day_time: f32 = 90.0;       // Morning; zero is sunrise.

// Compute lighting once per frame, not once per block. The meshes contain
// only static sky exposure, so a moving sun never needs to rebuild them.
fn sun_direction() -> float3 {
    let angle: f32 = day_time / DAY_LENGTH * TAU;
    normalize(float3 { -cos(angle), sin(angle), 0.3 })
}
fn daylight() -> f32 { clamp(sun_direction().y * 3.0 + 0.35, 0.0, 1.0) }
fn horizon() -> float3 {
    let day = daylight();
    let dusk = 1.0 - abs(day * 2.0 - 1.0);
    float3 { 0.045, 0.065, 0.12 } * f3(1.0 - day) +
        float3 { 0.70, 0.82, 0.87 } * f3(day) + float3 { 0.30, 0.04, 0.0 } * f3(dusk)
}
fn advance_time(dt: f32) { day_time = (day_time + dt) % DAY_LENGTH; }

struct Mesh {
    vertices: gfx::Buffer,
    indices: gfx::Buffer,
    count: i64,
    dirty: bool,
}
var meshes: Mesh[CHUNKS_X * CHUNKS_Z];
var lamps: float4[MAX_LAMPS];
var lamp_count = 0;
var message: u8[..100];
var message_time: f32 = 0.0;

// Cosmetic fragments are a fixed pool: breaking another block reuses the
// oldest slots. They never affect collisions, inventory, or saved games.
let MAX_DEBRIS = 48;
struct Debris { pos: float3, velocity: float3, life: f32, tile: i64 }
var debris: Debris[MAX_DEBRIS];
var next_debris = 0;

fn scatter(c: Cell, b: u8) {
    for i in 12 {
        let h = hash(c.x + i, c.y, c.z, game.seed);
        let velocity = float3 { (h & 255) / 64.0 - 2.0, 1.5 + ((h >> 8) & 255) / 128.0,
                                ((h >> 16) & 255) / 64.0 - 2.0 };
        debris[next_debris] = Debris {
            position(c) + f3(0.5) + velocity * f3(0.12), velocity, 0.45 + i * 0.012, tile_for(b, 4) };
        next_debris = (next_debris + 1) % MAX_DEBRIS;
    }
}
fn animate_debris(dt: f32) {
    for i in MAX_DEBRIS {
        if debris[i].life <= 0.0 { continue; }
        debris[i].life -= dt;
        debris[i].velocity.y -= 10.0 * dt;
        debris[i].pos += debris[i].velocity * f3(dt);
    }
}

fn say(text: const u8[:]) {
    message.resize(0);
    message.append(text);
    message_time = 4.0;
}

fn f3(n: f32) -> float3 { float3 { n, n, n } }
fn cell(x: i64, y: i64, z: i64) -> Cell { Cell { x, y, z } }
fn floor_cell(p: float3) -> Cell {
    Cell { floor(p.x) as i64, floor(p.y) as i64, floor(p.z) as i64 }
}
fn position(c: Cell) -> float3 { float3 { c.x, c.y, c.z } }
fn inside(c: Cell) -> bool {
    c.x >= 0 && c.x < WIDTH && c.y >= 0 && c.y < HEIGHT && c.z >= 0 && c.z < DEPTH
}
fn index(c: Cell) -> i64 { (c.y * DEPTH + c.z) * WIDTH + c.x }

fn block_at(c: Cell) -> u8 {
    if !inside(c) { return AIR; }
    blocks[index(c)]
}
fn solid(b: u8) -> bool { b != AIR && b != WATER }
fn put(c: Cell, b: u8) {
    if inside(c) { blocks[index(c)] = b; }
}
fn dirty_all() {
    for i in meshes.len { meshes[i].dirty = true; }
}
fn dirty_at(x: i64, z: i64) {
    if x >= 0 && x < WIDTH && z >= 0 && z < DEPTH {
        meshes[(z / CHUNK) * CHUNKS_X + x / CHUNK].dirty = true;
    }
}
fn find_lamps() {
    lamps = [float4_0; MAX_LAMPS];
    lamp_count = 0;
    for y in HEIGHT {
        for z in DEPTH {
            for x in WIDTH {
                if block_at(cell(x, y, z)) == LAMP && lamp_count < MAX_LAMPS {
                    lamps[lamp_count] = float4 { x + 0.5, y + 0.5, z + 0.5, 1.0 };
                    lamp_count++;
                }
            }
        }
    }
}
fn edit(c: Cell, b: u8) {
    let old = block_at(c);
    put(c, b);
    // Corner shading samples diagonal neighbours too. Rebuild all chunks
    // touched by a one-cell border; most edits still touch only one chunk.
    for dz in -1..2 {
        for dx in -1..2 { dirty_at(c.x + dx, c.z + dz); }
    }
    if old == LAMP || b == LAMP { find_lamps(); }
}

// --- deterministic terrain ---------------------------------------------------

// Unsigned arithmetic wraps. A coordinate hash needs no mutable random
// state, so adding a tree does not change the hills or texture patterns.
fn hash(x: i64, y: i64, z: i64, seed: u32) -> u32 {
    var h = (x as! u32) * 374761393 + (y as! u32) * 668265263;
    h += (z as! u32) * 2246822519 + seed * 3266489917;
    h = (h ^ (h >> 13)) * 1274126177;
    h ^ (h >> 16)
}
fn noise_value(x: i64, z: i64) -> f32 {
    (hash(x, 0, z, game.seed) & 65535) / 65535.0
}
fn noise(x: f32, z: f32) -> f32 {
    let ix = floor(x) as i64;
    let iz = floor(z) as i64;
    let fx = x - ix;
    let fz = z - iz;
    let u = fx * fx * (3.0 - 2.0 * fx);
    let v = fz * fz * (3.0 - 2.0 * fz);
    let a = noise_value(ix, iz) * (1.0 - u) + noise_value(ix + 1, iz) * u;
    let b = noise_value(ix, iz + 1) * (1.0 - u) + noise_value(ix + 1, iz + 1) * u;
    a * (1.0 - v) + b * v
}
fn ground_height(x: i64, z: i64) -> i64 {
    let broad = noise(x / 24.0, z / 24.0);
    let detail = noise(x / 8.0, z / 8.0);
    // A basin guarantees a lake; a broad ridge gives the cave a hillside.
    let lake = max(0.0, 1.0 - sqrt((x - 23.0) * (x - 23.0) +
                                  (z - 28.0) * (z - 28.0)) / 18.0);
    let ridge = max(0.0, 1.0 - abs(x - 58.0) / 15.0);
    let terrain = 13.0 + broad * 7.0 + detail * 3.0 - lake * 11.0 + ridge * 7.0;
    let distance = sqrt((x - 40.0) * (x - 40.0) + (z - 40.0) * (z - 40.0));
    let clearing = clamp((distance - 8.0) / 5.0, 0.0, 1.0);
    floor(19.0 * (1.0 - clearing) + terrain * clearing) as i64
}
fn highest(x: i64, z: i64) -> i64 {
    var y = HEIGHT - 1;
    while y > 0 && !solid(block_at(cell(x, y, z))) { y--; }
    y
}
fn tree(x: i64, z: i64) {
    let base = ground_height(x, z) + 1;
    if block_at(cell(x, base - 1, z)) != GRASS { return; }
    let height = 4 + (hash(x, 1, z, game.seed) % 2) as i64;
    for y in base + 3..base + height + 2 {
        let radius = if y == base + height + 1 { 1 } else { 2 };
        for dz in -radius..radius + 1 {
            for dx in -radius..radius + 1 {
                if abs(dx) == 2 && abs(dz) == 2 { continue; }
                let c = cell(x + dx, y, z + dz);
                if block_at(c) == AIR { put(c, LEAVES); }
            }
        }
    }
    for y in base..base + height { put(cell(x, y, z), WOOD); }
}
fn respawn() {
    // Search dry ground from the centre, without placing the player in a
    // trunk, a leaf canopy, a saved building, or the lake.
    for radius in 0..WIDTH / 2 {
        for z in max(1, 40 - radius)..min(DEPTH - 1, 41 + radius) {
            for x in max(1, 40 - radius)..min(WIDTH - 1, 41 + radius) {
                let y = highest(x, z);
                let b = block_at(cell(x, y, z));
                if (b == GRASS || b == SAND || b == STONE) && y >= SEA && y < HEIGHT - 3 {
                    game.player = Player {
                        feet: float3 { x + 0.5, y + 1.001, z + 0.5 },
                        velocity: 0.0, yaw: -0.4, pitch: -0.12, grounded: false };
                    return;
                }
            }
        }
    }
    game.player = Player { feet: float3 { 40.5, 41.0, 40.5 }, .. };
}
fn generate(seed: u32) {
    game = Game { version: 1, seed: seed, .. };
    day_time = 90.0;
    debris = [Debris { .. }; MAX_DEBRIS];
    message_time = 0.0;
    for i in VOLUME { blocks[i] = AIR; }
    for z in DEPTH {
        for x in WIDTH {
            let top = ground_height(x, z);
            for y in 0..max(top + 1, SEA + 1) {
                var b = STONE;
                if y == 0 { b = BEDROCK; }
                else if y > top { b = WATER; }
                else if y == top { b = if top <= SEA + 1 { SAND } else { GRASS }; }
                else if y >= top - 3 { b = if top <= SEA + 1 { SAND } else { DIRT }; }
                else {
                    // Coarse vein clusters with little ragged edges.
                    let vein = hash(x / 3, y / 3, z / 3, seed) % 13 == 0;
                    if vein && hash(x, y, z, seed) % 4 != 0 { b = COAL; }
                    let cave = sin(x * 0.24) + cos(z * 0.21) + sin(y * 0.55 + x * 0.11);
                    if y > 2 && y < top - 4 && cave > 2.25 { b = AIR; }
                }
                put(cell(x, y, z), b);
            }
        }
    }
    // A walk-in tunnel starts at the clearing and slopes into the ridge.
    // Carving overlapping little spheres is easy to read and modify.
    let entrance = ground_height(46, 40);
    for step in 22 {
        let cx: f32 = 45.0 + step * 0.65;
        let cy: f32 = entrance + 1.3 - step * 0.22;
        let cz: f32 = 40.0 - step * 0.38;
        for y in 2..HEIGHT - 1 {
            for z in 28..44 {
                for x in 43..63 {
                    let d = float3 { x + 0.5 - cx, y + 0.5 - cy, z + 0.5 - cz };
                    if length_sq(d) < 3.2 { put(cell(x, y, z), AIR); }
                }
            }
        }
    }
    for z in 4..DEPTH - 4 {
        for x in 4..WIDTH - 4 {
            let clearing = abs(x - 40) < 11 && abs(z - 40) < 11;
            if !clearing && x % 4 == 0 && z % 4 == 0 && hash(x, 5, z, seed) % 3 == 0 {
                tree(x, z);
            }
        }
    }
    tree(38, 34);           // The first resource is always a few steps away.
    respawn();
    find_lamps();
    dirty_all();
}

// --- walking, swimming, and a grid ray ---------------------------------------

let RADIUS: f32 = 0.29;
let BODY_HEIGHT: f32 = 1.8;
let EYE_HEIGHT: f32 = 1.62;
let STEP: f32 = 1.0 / 120.0;

fn eye() -> float3 { game.player.feet + float3 { 0.0, EYE_HEIGHT, 0.0 } }
fn forward() -> float3 {
    let p = game.player;
    float3 { sin(p.yaw) * cos(p.pitch), sin(p.pitch), -cos(p.yaw) * cos(p.pitch) }
}
fn overlaps(feet: float3, c: Cell) -> bool {
    feet.x + RADIUS > c.x && feet.x - RADIUS < c.x + 1 &&
    feet.y + BODY_HEIGHT > c.y && feet.y < c.y + 1 &&
    feet.z + RADIUS > c.z && feet.z - RADIUS < c.z + 1
}
fn blocked(feet: float3) -> bool {
    // The finite map's edges are walls, while its sky is open.
    if feet.x < RADIUS || feet.z < RADIUS || feet.x > WIDTH - RADIUS ||
       feet.z > DEPTH - RADIUS || feet.y < 1.0 { return true; }
    let lo = floor_cell(feet - float3 { RADIUS, 0.0, RADIUS });
    let hi = floor_cell(feet + float3 { RADIUS, BODY_HEIGHT - 0.001, RADIUS });
    for y in lo.y..hi.y + 1 {
        for z in lo.z..hi.z + 1 {
            for x in lo.x..hi.x + 1 {
                if solid(block_at(cell(x, y, z))) { return true; }
            }
        }
    }
    false
}
fn wet(feet: float3) -> bool {
    block_at(floor_cell(feet + float3 { 0.0, 0.05, 0.0 })) == WATER
}
fn supported(feet: float3) -> bool {
    // Leave a little overlap with the floor when sneaking onto a ledge.
    // Water does not count as support, and only the layer below the feet
    // matters: a nearby wall must not let us walk off a roof.
    let margin: f32 = RADIUS - 0.02;
    let lo = floor_cell(feet - float3 { margin, 0.05, margin });
    let hi = floor_cell(feet + float3 { margin, -0.05, margin });
    for z in lo.z..hi.z + 1 {
        for x in lo.x..hi.x + 1 {
            if solid(block_at(cell(x, lo.y, z))) { return true; }
        }
    }
    false
}
fn move_axis(delta: float3, keep_support: bool = false) -> bool {
    let start = game.player.feet;
    if !blocked(start + delta) && (!keep_support || supported(start + delta)) {
        game.player.feet += delta;
        return false;
    }
    // Find the contact point. The fixed timestep bounds each move to less
    // than a block, and bisection leaves the player just outside the wall.
    var lo: f32 = 0.0;
    var hi: f32 = 1.0;
    for i in 10 {
        let mid = (lo + hi) * 0.5;
        let candidate = start + delta * f3(mid);
        if blocked(candidate) || (keep_support && !supported(candidate)) { hi = mid; }
        else { lo = mid; }
    }
    game.player.feet += delta * f3(lo);
    true
}
fn walk(direction: float2, jump: bool, run: bool, sneak: bool = false) {
    let swimming = wet(game.player.feet);
    let speed: f32 = if swimming { 2.6 } else if sneak { 1.5 } else if run { 6.0 } else { 4.2 };
    let right = float3 { cos(game.player.yaw), 0.0, sin(game.player.yaw) };
    let ahead = float3 { sin(game.player.yaw), 0.0, -cos(game.player.yaw) };
    var input = direction;
    if length_sq(input) > 1.0 { input = normalize(input); }
    let delta = (right * f3(input.x) + ahead * f3(input.y)) * f3(speed * STEP);
    let keep_support = sneak && game.player.grounded && !jump && !swimming;
    move_axis(float3 { delta.x, 0.0, 0.0 }, keep_support);
    move_axis(float3 { 0.0, 0.0, delta.z }, keep_support);
    if swimming {
        game.player.velocity = max(game.player.velocity - 4.0 * STEP, -2.0);
        if jump {
            game.player.velocity = 4.0;
            // A kick near the surface clears a one-block shore. Testing
            // the feet for water keeps this active until we actually exit.
            if block_at(floor_cell(game.player.feet + float3 { 0.0, 1.0, 0.0 })) != WATER {
                game.player.velocity = 8.0;
            }
        }
    } else {
        if jump && game.player.grounded { game.player.velocity = 8.0; }
        game.player.velocity = max(game.player.velocity - 23.0 * STEP, -30.0);
    }
    let hit = move_axis(float3 { 0.0, game.player.velocity * STEP, 0.0 });
    game.player.grounded = hit && game.player.velocity < 0.0;
    if hit { game.player.velocity = 0.0; }
}

struct Hit { found: bool, at: Cell, before: Cell, distance: f32 }
fn ray(origin: float3, direction: float3, reach: f32) -> Hit {
    // DDA: cross exactly one grid plane each step. This neither skips thin
    // targets nor guesses the placement face from a small sampling step.
    var at = floor_cell(origin);
    var before = at;
    let step = Cell { if direction.x >= 0.0 { 1 } else { -1 },
                      if direction.y >= 0.0 { 1 } else { -1 },
                      if direction.z >= 0.0 { 1 } else { -1 } };
    let delta = float3 {
        if abs(direction.x) < 0.00001 { 1.0e8 } else { abs(1.0 / direction.x) },
        if abs(direction.y) < 0.00001 { 1.0e8 } else { abs(1.0 / direction.y) },
        if abs(direction.z) < 0.00001 { 1.0e8 } else { abs(1.0 / direction.z) } };
    var next = float3 {
        (if step.x > 0 { at.x + 1.0 - origin.x } else { origin.x - at.x }) * delta.x,
        (if step.y > 0 { at.y + 1.0 - origin.y } else { origin.y - at.y }) * delta.y,
        (if step.z > 0 { at.z + 1.0 - origin.z } else { origin.z - at.z }) * delta.z };
    var travelled: f32 = 0.0;
    while travelled <= reach {
        if solid(block_at(at)) { return Hit { true, at, before, travelled }; }
        before = at;
        if next.x <= next.y && next.x <= next.z {
            travelled = next.x;
            next.x += delta.x;
            at.x += step.x;
        } else if next.y <= next.z {
            travelled = next.y;
            next.y += delta.y;
            at.y += step.y;
        } else {
            travelled = next.z;
            next.z += delta.z;
            at.z += step.z;
        }
    }
    Hit { found: false, .. }
}

// --- mining, building, and four recipes --------------------------------------

fn drop_slot(b: u8) -> i64 {
    if b == GRASS || b == DIRT { return 0; }
    if b == STONE || b == COBBLE { return 1; }
    if b == COAL { return 7; }
    for i in SLOTS { if SLOT_BLOCK[i] == b { return i; } }
    -1
}
fn pick_material(hit: Hit) {
    if !hit.found { return; }
    let slot = drop_slot(block_at(hit.at));
    if slot >= 0 && SLOT_BLOCK[slot] != AIR {
        game.selected = slot as i32;
        if game.bag[slot] == 0 { say("MINE SOME OF THIS MATERIAL FIRST"); }
    }
}
fn mining_seconds(b: u8) -> f32 {
    if b == BEDROCK { return 0.0; }
    if b == STONE || b == COBBLE || b == COAL {
        if game.tool == 0 { return 0.0; }
        return if game.tool == 1 { 1.15 } else { 0.45 };
    }
    if b == WOOD { return 1.0; }
    if b == PLANKS { return 0.65; }
    if b == LEAVES || b == LAMP { return 0.18; }
    0.35
}
fn mine(c: Cell) -> bool {
    let b = block_at(c);
    let slot = drop_slot(b);
    if slot < 0 || mining_seconds(b) <= 0.0 { return false; }
    if game.bag[slot] >= 999 { say("THAT STACK IS FULL (999)"); return false; }
    game.bag[slot]++;
    edit(c, AIR);
    scatter(c, b);
    true
}
fn place(hit: Hit) -> bool {
    if !hit.found || hit.at == hit.before || !inside(hit.before) { return false; }
    let slot = game.selected;
    let b = SLOT_BLOCK[slot];
    if b == AIR { say("COAL IS FOR CRAFTING LAMPS"); return false; }
    if game.bag[slot] == 0 { say("MINE OR CRAFT SOME FIRST"); return false; }
    if solid(block_at(hit.before)) || overlaps(game.player.feet, hit.before) { return false; }
    if b == LAMP && lamp_count == MAX_LAMPS {
        say("SIXTEEN LIGHTS MAX - PICK ONE UP FIRST");
        return false;
    }
    edit(hit.before, b);
    game.bag[slot]--;
    true
}
fn can_craft(recipe: i64) -> bool {
    if recipe == 0 { return game.bag[2] >= 1 && game.bag[3] <= 995; }
    if recipe == 1 { return game.tool == 0 && game.bag[3] >= 5; }
    if recipe == 2 { return game.tool == 1 && game.bag[1] >= 3 && game.bag[3] >= 2; }
    if recipe == 3 { return game.bag[7] >= 1 && game.bag[3] >= 1 && game.bag[6] <= 995; }
    false
}
fn craft(recipe: i64) -> bool {
    if !can_craft(recipe) {
        say("NOT ENOUGH MATERIALS, OR UPGRADE ALREADY OWNED");
        return false;
    }
    if recipe == 0 {
        game.bag[2]--;
        game.bag[3] += 4;
        say("MADE FOUR PLANKS");
        return true;
    }
    if recipe == 1 {
        game.bag[3] -= 5;
        game.tool = 1;
        say("WOODEN PICK - NOW YOU CAN MINE STONE");
        return true;
    }
    if recipe == 2 {
        game.bag[1] -= 3;
        game.bag[3] -= 2;
        game.tool = 2;
        say("STONE PICK - FASTER MINING");
        return true;
    }
    if recipe == 3 {
        game.bag[7]--;
        game.bag[3]--;
        game.bag[6] += 4;
        say("MADE FOUR LAMPS - SELECT SLOT 7 TO PLACE");
        return true;
    }
    false
}
fn craft_batches(recipe: i64, all: bool) -> i64 {
    if !craft(recipe) { return 0; }
    var batches = 1;
    // can_craft checks both ingredients and stack capacity. Tools become
    // unavailable once owned, so even "craft all" makes only one upgrade.
    while all && can_craft(recipe) {
        craft(recipe);
        batches++;
    }
    if batches > 1 {
        say(str("MADE ", batches * 4, if recipe == 0 { " PLANKS" } else { " LAMPS" }));
    }
    batches
}

// --- explicit saves ----------------------------------------------------------

fn valid_save(g: const Game&, cells: const u8[:]) -> bool {
    if cells.len != VOLUME { return false; }
    if g.version != 1 || g.tool < 0 || g.tool > 2 || g.selected < 0 || g.selected >= SLOTS {
        return false;
    }
    for n in g.bag { if n < 0 || n > 999 { return false; } }
    var lights = 0;
    for b in cells {
        if b > LAMP { return false; }
        if b == LAMP { lights++; }
    }
    if lights > MAX_LAMPS { return false; }
    let p = g.player;
    // Positive tests reject NaNs as well as out-of-range values.
    if !(p.feet.x >= RADIUS && p.feet.x <= WIDTH - RADIUS &&
         p.feet.z >= RADIUS && p.feet.z <= DEPTH - RADIUS &&
         p.feet.y >= 1.0 && p.feet.y <= HEIGHT + 8.0 &&
         p.yaw >= -100.0 && p.yaw <= 100.0 && abs(p.pitch) <= 1.5 &&
         abs(p.velocity) <= 30.0) { return false; }
    for z in DEPTH {
        for x in WIDTH { if cells[z * WIDTH + x] != BEDROCK { return false; } }
    }
    true
}
fn save_image() -> u8[>..] {
    var image: u8[>..] = [];
    image.append("GSM2");
    to_bytes([game], image);
    image.append(blocks);
    to_bytes([day_time], image);
    image
}
fn restore(image: const u8[:]) -> bool {
    // The header uses Goose's framed serialization; the grid is just bytes
    // of block IDs. Validate both before changing anything in the game.
    let header_size = to_bytes([Game { .. }]).len;
    let grid_end = 4 + header_size + VOLUME;
    let clock_size = to_bytes([day_time]).len;
    if image.len < 4 { return false; }
    let legacy = image[..4] == "GSM1";
    if legacy {
        if image.len != grid_end { return false; }
    } else if image[..4] != "GSM2" || image.len != grid_end + clock_size { return false; }
    var header, ok = from_bytes<Game[>..]>(image[4..4 + header_size]);
    if !ok || header.len != 1 { return false; }
    let saved_blocks = image[4 + header_size..grid_end];
    if !valid_save(header[0], saved_blocks) { return false; }
    // The first version had no clock. Old worlds resume in the morning.
    var saved_time: f32 = 90.0;
    if !legacy {
        let clock, clock_ok = from_bytes<f32[>..]>(image[grid_end..]);
        if !clock_ok || clock.len != 1 || !(clock[0] >= 0.0 && clock[0] < DAY_LENGTH) { return false; }
        saved_time = clock[0];
    }
    game = header[0];
    day_time = saved_time;
    debris = [Debris { .. }; MAX_DEBRIS];
    for i in VOLUME { blocks[i] = saved_blocks[i]; }
    if blocked(game.player.feet) { respawn(); }
    find_lamps();
    dirty_all();
    true
}
fn save_game() {
    let image = save_image();
    if write_file_atomic("mini_minecraft.sav", image) { say("SAVED MINI_MINECRAFT.SAV"); }
    else { say("SAVE FAILED - CHECK THE WORKING DIRECTORY"); }
}
fn load_game() {
    var image: u8[>..] = [];
    if !read_file("mini_minecraft.sav", image) { say("NO READABLE MINI_MINECRAFT.SAV"); return; }
    if !restore(image) { say("SAVE FILE IS DAMAGED OR INCOMPATIBLE"); return; }
    say("WORLD LOADED");
}

// --- a 256 x 16 texture atlas, painted one pixel at a time --------------------

let TILE_SIZE = 16;
let TILES = 16;
let WHITE_TILE = 14;
let TILE_COLOR: int3[TILES] = [
    int3 { 101, 153, 58 }, int3 { 126, 89, 55 }, int3 { 126, 89, 55 },
    int3 { 129, 132, 130 }, int3 { 220, 204, 150 }, int3 { 106, 76, 43 },
    int3 { 173, 137, 81 }, int3 { 65, 120, 48 }, int3 { 177, 135, 79 },
    int3 { 126, 131, 133 }, int3 { 121, 126, 128 }, int3 { 52, 126, 176 },
    int3 { 61, 64, 74 }, int3 { 244, 198, 94 }, int3 { 255, 255, 255 },
    int3 { 255, 255, 255 } ];

fn tile_for(b: u8, face: i64) -> i64 {
    if b == GRASS { return if face == 2 { 0 } else if face == 3 { 2 } else { 1 }; }
    if b == WOOD { return if face == 2 || face == 3 { 6 } else { 5 }; }
    if b == DIRT { return 2; }
    if b == STONE { return 3; }
    if b == SAND { return 4; }
    if b == LEAVES { return 7; }
    if b == PLANKS { return 8; }
    if b == COBBLE { return 9; }
    if b == COAL { return 10; }
    if b == WATER { return 11; }
    if b == BEDROCK { return 12; }
    if b == LAMP { return 13; }
    WHITE_TILE
}
fn pixel(tile: i64, x: i64, y: i64) -> u8[4] {
    var color = TILE_COLOR[tile];
    var grain = (hash(x, tile, y, 71) % 27) as i64 - 13;
    if tile == 1 && y < 3 + (hash(x / 2, 0, 0, 8) % 3) as i64 {
        color = TILE_COLOR[0];
    }
    if tile == 3 { grain += if (x + y * 3) % 11 == 0 { -20 } else { 0 }; }
    if tile == 5 { grain += if (x + (y / 5)) % 4 == 0 { -30 } else { 9 }; }
    if tile == 6 {
        let ring = max(abs(x - 7), abs(y - 7));
        grain += if ring % 3 == 0 { -30 } else { 12 };
    }
    if tile == 7 {
        grain += if hash(x / 2, 0, y / 2, 17) % 3 == 0 { -28 } else { 10 };
    }
    if tile == 8 {
        if y % 4 == 0 || (x + (y / 4) * 7) % 16 == 0 { grain = -42; }
        else { grain += if y % 4 == 1 { 12 } else { 0 }; }
    }
    if tile == 9 {
        let row = y / 5;
        if y % 5 == 0 || (x + row * 3) % 7 == 0 { grain = -45; }
        else if y % 5 == 1 { grain = 18; }
    }
    if tile == 10 && hash(x / 2, 0, y / 2, 9) % 5 == 0 { color = int3 { 32, 34, 39 }; }
    if tile == 11 { grain = if (x + y * 3) % 13 < 2 { 20 } else { 0 }; }
    if tile == 12 { grain *= 2; }
    if tile == 13 {
        if x < 2 || x > 13 || y < 2 || y > 13 { color = int3 { 94, 66, 39 }; }
        else { grain = if x == 7 || y == 7 { 25 } else { 0 }; }
    }
    if tile >= WHITE_TILE { grain = 0; }
    [clamp(color.x + grain, 0, 255) as u8, clamp(color.y + grain, 0, 255) as u8,
     clamp(color.z + grain, 0, 255) as u8, 255]
}
fn make_atlas() -> gfx::Texture {
    var pixels: u8[>..] = [];
    for y in TILE_SIZE {
        for tile in TILES {
            for x in TILE_SIZE { pixels.append(pixel(tile, x, y)); }
        }
    }
    let texture = gfx::texture2d(TILE_SIZE * TILES, TILE_SIZE);
    if texture.id == 0 || !gfx::update_texture(texture, pixels) {
        abort(str("texture: ", gfx::error()));
    }
    texture
}
fn atlas_uv(tile: i64, x: f32, y: f32) -> float2 {
    // Sample inside the tile, so the nearest sampler never grabs a pixel
    // from its neighbour, even right at the edge of a face.
    float2 { (tile * TILE_SIZE + 0.5 + x * 15.0) / (TILE_SIZE * TILES),
             (0.5 + y * 15.0) / TILE_SIZE }
}

// --- exposed faces, split into 16 x 40 x 16 chunks ----------------------------

struct Vertex {
    pos: float3,
    normal: float3,
    uv: float2,
    shade: f32,
    material: f32,
}
let NORMAL: Cell[6] = [Cell { 1, 0, 0 }, Cell { -1, 0, 0 },
                        Cell { 0, 1, 0 }, Cell { 0, -1, 0 },
                        Cell { 0, 0, 1 }, Cell { 0, 0, -1 }];
let ACROSS: Cell[6] = [Cell { 0, 0, -1 }, Cell { 0, 0, 1 },
                        Cell { 1, 0, 0 }, Cell { 1, 0, 0 },
                        Cell { 1, 0, 0 }, Cell { -1, 0, 0 }];
let UP: Cell[6] = [Cell { 0, 1, 0 }, Cell { 0, 1, 0 },
                    Cell { 0, 0, -1 }, Cell { 0, 0, 1 },
                    Cell { 0, 1, 0 }, Cell { 0, 1, 0 }];
let CORNERS: float2[4] = [float2 { -1.0, -1.0 }, float2 { 1.0, -1.0 },
                          float2 { 1.0, 1.0 }, float2 { -1.0, 1.0 }];
var column_top: i64[WIDTH * DEPTH];

fn offset(c: Cell, dir: Cell, amount: i64) -> Cell {
    Cell { c.x + dir.x * amount, c.y + dir.y * amount, c.z + dir.z * amount }
}
fn sky_light(c: Cell) -> f32 {
    if c.x < 0 || c.x >= WIDTH || c.z < 0 || c.z >= DEPTH { return 1.0; }
    if c.y > column_top[c.z * WIDTH + c.x] { return 1.0; }
    0.32
}
fn face_visible(b: u8, neighbour: u8) -> bool {
    if b == WATER { return neighbour == AIR; }
    neighbour == AIR || neighbour == WATER
}
fn add_face(vertices: Vertex[>..]&, indices: u32[>..]&, c: Cell, b: u8, face: i64) {
    let n = NORMAL[face];
    let u = ACROSS[face];
    let v = UP[face];
    let outside = c + n;
    let base = vertices.len as u32;
    for corner in CORNERS {
        let side_u = offset(outside, u, corner.x as i64);
        let side_v = offset(outside, v, corner.y as i64);
        let diagonal = offset(side_u, v, corner.y as i64);
        let a = solid(block_at(side_u));
        let d = solid(block_at(side_v));
        let e = solid(block_at(diagonal));
        // Three neighbouring blocks darken a corner; two touching sides
        // close it completely, even if the diagonal happens to be air.
        var occlusion = 0;
        if a { occlusion++; }
        if d { occlusion++; }
        if e { occlusion++; }
        if a && d { occlusion = 3; }
        let shade = sky_light(outside) * (1.0 - occlusion * 0.16);
        let p = position(c) + f3(0.5) + position(n) * f3(0.5) +
                position(u) * f3(corner.x * 0.5) + position(v) * f3(corner.y * 0.5);
        let uv = atlas_uv(tile_for(b, face), corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
        vertices.push(Vertex { p, position(n), uv, shade, b });
    }
    indices.append([base, base + 1, base + 2, base, base + 2, base + 3]);
}
fn rebuild_chunk(cx: i64, cz: i64) {
    // Scratch lives only until this function returns. The GPU owns a copy.
    var vertices: Vertex[>..] = [];
    var indices: u32[>..] = [];
    for y in HEIGHT {
        for z in cz * CHUNK..(cz + 1) * CHUNK {
            for x in cx * CHUNK..(cx + 1) * CHUNK {
                let c = cell(x, y, z);
                let b = block_at(c);
                if b == AIR { continue; }
                for face in 6 {
                    if face_visible(b, block_at(c + NORMAL[face])) {
                        add_face(vertices, indices, c, b, face);
                    }
                }
            }
        }
    }
    let i = cz * CHUNKS_X + cx;
    if meshes[i].vertices.id != 0 { gfx::release(meshes[i].vertices); }
    if meshes[i].indices.id != 0 { gfx::release(meshes[i].indices); }
    meshes[i] = Mesh { count: indices.len, .. };
    if indices.len > 0 {
        meshes[i].vertices = gfx::vertex_buffer(bytes_of(vertices));
        meshes[i].indices = gfx::index_buffer(bytes_of(indices));
        if meshes[i].vertices.id == 0 || meshes[i].indices.id == 0 {
            abort(str("chunk buffers: ", gfx::error()));
        }
    }
}
fn rebuild_dirty() {
    var any = false;
    for m in meshes { any = any || m.dirty; }
    if !any { return; }
    for z in DEPTH {
        for x in WIDTH { column_top[z * WIDTH + x] = highest(x, z); }
    }
    for cz in CHUNKS_Z {
        for cx in CHUNKS_X {
            if meshes[cz * CHUNKS_X + cx].dirty { rebuild_chunk(cx, cz); }
        }
    }
}

fn debris_vertices(right: float3, up: float3) -> Vertex[>..] {
    // Camera-facing squares reuse the world's texture atlas and lighting.
    // Only a small patch of each tile is needed for a chip of that material.
    var vertices: Vertex[>..] = [];
    let order: i64[6] = [0, 1, 2, 0, 2, 3];
    let normal = cross(right, up);
    for bit in debris {
        if bit.life <= 0.0 { continue; }
        let size: f32 = 0.065 * min(1.0, bit.life * 8.0);
        for i in order {
            let corner = CORNERS[i];
            let p = bit.pos + right * f3(corner.x * size) + up * f3(corner.y * size);
            let uv = atlas_uv(bit.tile, 0.5 + corner.x * 0.125, 0.5 - corner.y * 0.125);
            vertices.push(Vertex { p, normal, uv, sky_light(floor_cell(p)), -1.0 });
        }
    }
    vertices
}

// --- shaders ----------------------------------------------------------------

// Every field begins on a 16-byte boundary, exactly as GLSL std140 wants.
// eye.w is time; target.w is mining progress, or -1 if nothing is targeted.
struct Scene {
    view_proj: gfx::mat4,
    eye: float4,
    target: float4,
    lights: float4[MAX_LAMPS],
    haze: float4,
    sun: float4,              // xyz: direction, w: daylight strength
}
let scene_uniform = """
    layout(set = 3, binding = 0) uniform Scene {
        mat4 view_proj;
        vec4 eye;
        vec4 target;
        vec4 lights[16];
        vec4 haze;
        vec4 sun;
    };
    """;
let world_vertex = """
    layout(location = 0) in vec3 a_pos;
    layout(location = 1) in vec3 a_normal;
    layout(location = 2) in vec2 a_uv;
    layout(location = 3) in float a_shade;
    layout(location = 4) in float a_material;
    layout(set = 1, binding = 0) uniform Camera { mat4 view_proj; };
    layout(location = 0) out vec3 v_world;
    layout(location = 1) out vec3 v_normal;
    layout(location = 2) out vec2 v_uv;
    layout(location = 3) out float v_shade;
    layout(location = 4) flat out float v_material;
    void main() {
        v_world = a_pos;
        v_normal = a_normal;
        v_uv = a_uv;
        v_shade = a_shade;
        v_material = a_material;
        gl_Position = view_proj * vec4(a_pos, 1.0);
    }
    """;
let world_fragment = """
    layout(location = 0) in vec3 v_world;
    layout(location = 1) in vec3 v_normal;
    layout(location = 2) in vec2 v_uv;
    layout(location = 3) in float v_shade;
    layout(location = 4) flat in float v_material;
    layout(set = 2, binding = 0) uniform sampler2D atlas;
    layout(location = 0) out vec4 result;
    void main() {
        vec3 base = texture(atlas, v_uv).rgb;
        float direct = max(dot(v_normal, sun.xyz), 0.0) * sun.w;
        vec3 light = mix(vec3(0.10, 0.14, 0.23), vec3(0.64, 0.73, 0.82) * 0.48, sun.w);
        light += vec3(0.66, 0.59, 0.43) * direct;
        light *= v_shade;
        for (int i = 0; i < 16; i++) {
            float d = length(lights[i].xyz - v_world);
            float glow = max(0.0, 1.0 - d / 7.0);
            light += vec3(1.0, 0.63, 0.25) * glow * glow * lights[i].w;
        }
        if (v_material == 12.0) light = max(light, vec3(1.1, 0.96, 0.7));
        if (v_material == 10.0) {
            float wave = sin(v_world.x * 2.7 + eye.w) * sin(v_world.z * 3.1 - eye.w);
            base += vec3(0.04, 0.07, 0.08) * wave;
            light = max(light, vec3(mix(0.15, 0.72, sun.w)));
        }
        vec3 color = base * light;
        vec3 cell = floor(v_world - v_normal * 0.002);
        bool targeted = all(lessThan(abs(cell - target.xyz), vec3(0.1)));
        if (v_material >= 0.0 && target.w >= 0.0 && targeted) {
            vec3 local = fract(v_world);
            vec3 edge = min(local, 1.0 - local) + abs(v_normal);
            float border = min(edge.x, min(edge.y, edge.z));
            if (border < 0.018) color = vec3(1.0, 0.91, 0.57);
            // A progressively denser cracked pattern while holding mine.
            vec3 crack = abs(fract(v_world * 5.0 + sin(v_world.zxy * 13.0) * 0.2) - 0.5);
            if (target.w > 0.0 && min(crack.x + crack.y, crack.y + crack.z) < target.w * 0.18)
                color *= 0.28;
        }
        float fog = smoothstep(34.0, 83.0, length(eye.xyz - v_world));
        if (haze.w > 0.0) fog = 1.0 - exp(-length(eye.xyz - v_world) * 0.17);
        result = vec4(mix(color, haze.rgb, fog), 1.0);
    }
    """;
let sky_vertex = """
    layout(location = 0) out vec2 v_uv;
    void main() {
        vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
        v_uv = p;
        gl_Position = vec4(p * 2.0 - 1.0, 0.9999, 1.0);
    }
    """;
struct Sky { ahead: float4, right: float4, up: float4, info: float4, sun: float4, haze: float4 }
let sky_fragment = """
    layout(location = 0) in vec2 v_uv;
    layout(location = 0) out vec4 result;
    layout(set = 3, binding = 0) uniform Sky {
        vec4 ahead; vec4 right; vec4 up; vec4 info; vec4 sun; vec4 haze;
    };
    void main() {
        vec2 p = v_uv * 2.0 - 1.0;
        vec3 ray = normalize(ahead.xyz + right.xyz * p.x * info.x + up.xyz * p.y);
        vec3 zenith = mix(vec3(0.008, 0.015, 0.045), vec3(0.29, 0.57, 0.79), sun.w);
        vec3 color = mix(haze.rgb, zenith,
                         clamp(ray.y * 1.4, 0.0, 1.0));
        float glow = pow(max(dot(ray, sun.xyz), 0.0), 32.0);
        color += vec3(0.18, 0.14, 0.05) * glow * sun.w;
        if (ray.y > 0.0 && dot(ray, sun.xyz) > 0.9975) color = vec3(1.0, 0.95, 0.72);
        if (ray.y > 0.0 && dot(ray, -sun.xyz) > 0.9985) color = vec3(0.72, 0.79, 0.9);
        // A sparse hash of direction cells makes stars stay fixed as we turn.
        vec3 star_cell = floor(ray * 220.0);
        float star = fract(sin(dot(star_cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        if (ray.y > 0.12 && star > 0.9985) color += vec3(0.65) * (1.0 - sun.w);
        if (ray.y > 0.08) {
            vec2 p = ray.xz * (30.0 / ray.y) + vec2(info.y * 0.22, 0.0);
            vec2 grid = floor(p / 5.0);
            float n = sin(grid.x * 1.7 + sin(grid.y * 2.3)) + cos(grid.y * 1.2);
            vec3 cloud = mix(vec3(0.09, 0.12, 0.20), vec3(0.95, 0.96, 0.92), sun.w);
            if (n > 0.8) color = mix(color, cloud, smoothstep(0.08, 0.3, ray.y));
        }
        if (info.z > 0.0) color = vec3(0.08, 0.28, 0.38);
        result = vec4(color, 1.0);
    }
    """;

// --- a tiny immediate-mode HUD, also drawn with gfx ---------------------------

// A 5 x 7 font is enough for this sample. Each row is a five-bit mask.
// No font file, font rasterizer, or text texture is needed: set bits become
// little rectangles in the same batch as the panels and inventory icons.
fn glyph(c: u8) -> u8[7] {
    match c {
        'A' => [14, 17, 17, 31, 17, 17, 17], 'B' => [30, 17, 17, 30, 17, 17, 30],
        'C' => [14, 17, 16, 16, 16, 17, 14], 'D' => [30, 17, 17, 17, 17, 17, 30],
        'E' => [31, 16, 16, 30, 16, 16, 31], 'F' => [31, 16, 16, 30, 16, 16, 16],
        'G' => [14, 17, 16, 23, 17, 17, 15], 'H' => [17, 17, 17, 31, 17, 17, 17],
        'I' => [14, 4, 4, 4, 4, 4, 14], 'J' => [7, 2, 2, 2, 18, 18, 12],
        'K' => [17, 18, 20, 24, 20, 18, 17], 'L' => [16, 16, 16, 16, 16, 16, 31],
        'M' => [17, 27, 21, 21, 17, 17, 17], 'N' => [17, 25, 21, 19, 17, 17, 17],
        'O' => [14, 17, 17, 17, 17, 17, 14], 'P' => [30, 17, 17, 30, 16, 16, 16],
        'Q' => [14, 17, 17, 17, 21, 18, 13], 'R' => [30, 17, 17, 30, 20, 18, 17],
        'S' => [15, 16, 16, 14, 1, 1, 30], 'T' => [31, 4, 4, 4, 4, 4, 4],
        'U' => [17, 17, 17, 17, 17, 17, 14], 'V' => [17, 17, 17, 17, 17, 10, 4],
        'W' => [17, 17, 17, 21, 21, 21, 10], 'X' => [17, 17, 10, 4, 10, 17, 17],
        'Y' => [17, 17, 10, 4, 4, 4, 4], 'Z' => [31, 1, 2, 4, 8, 16, 31],
        '0' => [14, 17, 19, 21, 25, 17, 14], '1' => [4, 12, 4, 4, 4, 4, 14],
        '2' => [14, 17, 1, 2, 4, 8, 31], '3' => [30, 1, 1, 14, 1, 1, 30],
        '4' => [2, 6, 10, 18, 31, 2, 2], '5' => [31, 16, 16, 30, 1, 1, 30],
        '6' => [14, 16, 16, 30, 17, 17, 14], '7' => [31, 1, 2, 4, 8, 8, 8],
        '8' => [14, 17, 17, 14, 17, 17, 14], '9' => [14, 17, 17, 15, 1, 1, 14],
        '-' => [0, 0, 0, 31, 0, 0, 0], '+' => [0, 4, 4, 31, 4, 4, 0],
        '.' => [0, 0, 0, 0, 0, 12, 12], ':' => [0, 12, 12, 0, 12, 12, 0],
        '/' => [1, 2, 2, 4, 8, 8, 16], '(' => [2, 4, 8, 8, 8, 4, 2],
        ')' => [8, 4, 2, 2, 2, 4, 8], _ => [0; 7],
    }
}
struct HudVertex { pos: float2, uv: float2, color: u8[4] }
let INK: u8[4] = [235, 237, 223, 255];
let MUTED: u8[4] = [166, 183, 181, 255];
let GOLD: u8[4] = [250, 209, 116, 255];
let PANEL: u8[4] = [22, 34, 40, 225];
let WHITE: u8[4] = [255, 255, 255, 255];

fn quad(out: HudVertex[>..]&, x: f32, y: f32, w: f32, h: f32, color: u8[4], tile: i64) {
    let a = HudVertex { float2 { x, y }, atlas_uv(tile, 0.0, 0.0), color };
    let b = HudVertex { float2 { x + w, y }, atlas_uv(tile, 1.0, 0.0), color };
    let c = HudVertex { float2 { x + w, y + h }, atlas_uv(tile, 1.0, 1.0), color };
    let d = HudVertex { float2 { x, y + h }, atlas_uv(tile, 0.0, 1.0), color };
    out.append([a, b, c, a, c, d]);
}
fn rect(out: HudVertex[>..]&, x: f32, y: f32, w: f32, h: f32, color: u8[4]) {
    quad(out, x, y, w, h, color, WHITE_TILE);
}
fn text(out: HudVertex[>..]&, x: f32, y: f32, words: const u8[:], color: u8[4], scale: f32) {
    var at = x;
    for c in words {
        let rows = glyph(c);
        for row in 7 {
            for col in 5 {
                if (rows[row] & (1 << (4 - col))) != 0 {
                    rect(out, at + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        at += 6.0 * scale;
    }
}
fn centred(out: HudVertex[>..]&, middle: f32, y: f32, words: const u8[:], color: u8[4]) {
    text(out, middle - words.len * 6.0, y, words, color, 2.0);
}
fn next_goal() -> const u8[:] {
    if game.tool == 0 && game.bag[2] < 2 && game.bag[3] < 5 { return "GATHER TWO WOOD BLOCKS"; }
    if game.tool == 0 { return "C - MAKE PLANKS AND A WOODEN PICK"; }
    if game.tool == 1 { return "MINE THREE STONE - C TO UPGRADE"; }
    if game.bag[6] == 0 && lamp_count == 0 { return "FIND COAL - C TO CRAFT LAMPS"; }
    "BUILD A CABIN - LIGHT YOUR CAVE"
}
fn block_name(b: u8) -> const u8[:] {
    if b == BEDROCK { return "BEDROCK - UNBREAKABLE"; }
    if (b == STONE || b == COAL || b == COBBLE) && game.tool == 0 { return "NEEDS A PICK - PRESS C"; }
    if b == GRASS { return "GRASS"; }
    if b == COAL { return "COAL ORE"; }
    let slot = drop_slot(b);
    if slot >= 0 { return SLOT_NAME[slot]; }
    ""
}
fn hud(out: HudVertex[>..]&, size: float2, hit: Hit, progress: f32,
       crafting: bool, paused: bool, help: bool) {
    rect(out, 18.0, 18.0, 418.0, 99.0, PANEL);
    rect(out, 18.0, 18.0, 4.0, 99.0, GOLD);
    text(out, 34.0, 31.0, "MINI MINECRAFT", GOLD, 2.0);
    text(out, 34.0, 56.0, next_goal(), INK, 1.5);
    let tool = if game.tool == 0 { "HANDS" } else if game.tool == 1 { "WOODEN PICK" } else { "STONE PICK" };
    text(out, 34.0, 79.0, str(tool, "  /  SEED ", game.seed), MUTED, 1.0);
    let phase = if daylight() < 0.2 { "NIGHT - LIGHT YOUR HOME" }
                else if day_time > 450.0 { "DAWN" }
                else if day_time > 270.0 { "DUSK - CRAFT SOME LAMPS" }
                else if day_time < 150.0 { "MORNING" } else { "AFTERNOON" };
    text(out, 34.0, 95.0, phase, MUTED, 1.0);

    if help && !crafting && !paused {
        let x = size.x - 316.0;
        rect(out, x, 18.0, 298.0, 192.0, PANEL);
        text(out, x + 14.0, 31.0, "MAKE YOURSELF AT HOME", GOLD, 1.5);
        let lines: const u8[:][9] = [
            "WASD WALK  /  SPACE JUMP OR SWIM", "SHIFT RUN  /  ARROWS LOOK",
            "CTRL SNEAK - STOPS AT LEDGES",
            "MOUSE LOOK  /  LEFT HOLD MINE", "RIGHT OR E PLACE - HOLD TO BUILD",
            "MIDDLE CLICK PICKS MATERIAL",
            "1-8 OR WHEEL SELECT  /  C CRAFT", "F5 SAVE  /  F9 LOAD  /  F12 PHOTO",
            "ESC PAUSE  /  H HIDE HELP" ];
        for i in lines.len { text(out, x + 14.0, 58.0 + i * 16.0, lines[i], INK, 1.0); }
    } else if !crafting && !paused { text(out, size.x - 132.0, 26.0, "H - HELP", INK, 1.5); }

    let bar_x = (size.x - SLOTS * 66.0) * 0.5;
    let bar_y = size.y - 82.0;
    for i in SLOTS {
        let x = bar_x + i * 66.0;
        rect(out, x, bar_y, 62.0, 64.0, if game.selected == i { GOLD } else { PANEL });
        rect(out, x + 3.0, bar_y + 3.0, 56.0, 58.0, PANEL);
        let tile = if i == 7 { 10 } else { tile_for(SLOT_BLOCK[i], 4) };
        let tint: u8[4] = if game.bag[i] == 0 { [105, 110, 113, 255] } else { WHITE };
        quad(out, x + 17.0, bar_y + 8.0, 28.0, 28.0, tint, tile);
        text(out, x + 7.0, bar_y + 8.0, str(i + 1), GOLD, 1.0);
        text(out, x + 7.0, bar_y + 40.0, SLOT_NAME[i], MUTED, 1.0);
        text(out, x + 7.0, bar_y + 51.0, str(game.bag[i]), INK, 1.0);
    }
    centred(out, size.x * 0.5, bar_y - 25.0, SLOT_NAME[game.selected], INK);
    if !crafting && !paused {
        let cx = size.x * 0.5;
        let cy = size.y * 0.5;
        rect(out, cx - 7.0, cy - 2.0, 15.0, 5.0, PANEL);
        rect(out, cx - 2.0, cy - 7.0, 5.0, 15.0, PANEL);
        rect(out, cx - 6.0, cy, 13.0, 1.0, INK);
        rect(out, cx, cy - 6.0, 1.0, 13.0, INK);
        if hit.found {
            let label = block_name(block_at(hit.at));
            text(out, cx - label.len * 3.0, cy + 22.0, label, INK, 1.0);
            if progress > 0.0 {
                rect(out, cx - 40.0, cy + 37.0, 80.0, 5.0, PANEL);
                rect(out, cx - 40.0, cy + 37.0, 80.0 * progress, 5.0, GOLD);
            }
        }
    }
    if message_time > 0.0 {
        let width: f32 = message.len * 9.0 + 28.0;
        rect(out, (size.x - width) * 0.5, bar_y - 65.0, width, 28.0, PANEL);
        text(out, (size.x - width) * 0.5 + 14.0, bar_y - 57.0, message, GOLD, 1.5);
    }
    if crafting {
        let x = (size.x - 504.0) * 0.5;
        let y = (size.y - 300.0) * 0.5;
        rect(out, x, y, 504.0, 300.0, PANEL);
        rect(out, x, y, 504.0, 3.0, GOLD);
        text(out, x + 22.0, y + 20.0, "CRAFTING", GOLD, 2.0);
        text(out, x + 22.0, y + 48.0, "CLICK A RECIPE OR PRESS ITS NUMBER", MUTED, 1.5);
        let titles: const u8[:][4] = ["1  FOUR PLANKS", "2  WOODEN PICK", "3  STONE PICK", "4  FOUR LAMPS"];
        let costs: const u8[:][4] = ["1 WOOD", "5 PLANKS", "3 STONE + 2 PLANKS + WOODEN PICK",
                                      "1 COAL + 1 PLANK"];
        for i in 4 {
            let row_y = y + 78.0 + i * 46.0;
            rect(out, x + 16.0, row_y, 472.0, 40.0, [42, 57, 60, 250]);
            let ready = can_craft(i);
            text(out, x + 28.0, row_y + 7.0, titles[i], if ready { INK } else { MUTED }, 1.5);
            let owned = (i == 1 && game.tool >= 1) || (i == 2 && game.tool >= 2);
            text(out, x + 424.0, row_y + 8.0, if ready { "READY" } else if owned { "OWNED" } else { "NEED" },
                 if ready { GOLD } else { MUTED }, 1.0);
            text(out, x + 28.0, row_y + 26.0, costs[i], MUTED, 1.0);
        }
        text(out, x + 22.0, y + 264.0, "HOLD SHIFT WITH A RECIPE TO CRAFT ALL.", GOLD, 1.0);
        text(out, x + 22.0, y + 280.0, "TOOLS EQUIP AUTOMATICALLY. C OR ESC CLOSES.", MUTED, 1.0);
    } else if paused {
        rect(out, (size.x - 400.0) * 0.5, size.y * 0.5 - 45.0, 400.0, 90.0, PANEL);
        centred(out, size.x * 0.5, size.y * 0.5 - 26.0, "PAUSED", GOLD);
        text(out, size.x * 0.5 - 162.0, size.y * 0.5 + 4.0,
             "CLICK OR ESC TO RESUME. F5 SAVES.", INK, 1.5);
    }
}
let hud_vertex = """
    layout(location = 0) in vec2 a_pos;
    layout(location = 1) in vec2 a_uv;
    layout(location = 2) in vec4 a_color;
    layout(set = 1, binding = 0) uniform Screen { vec4 size; };
    layout(location = 0) out vec2 v_uv;
    layout(location = 1) out vec4 v_color;
    void main() {
        v_uv = a_uv; v_color = a_color;
        gl_Position = vec4(a_pos.x / size.x * 2.0 - 1.0, 1.0 - a_pos.y / size.y * 2.0, 0.0, 1.0);
    }
    """;
let hud_fragment = """
    layout(location = 0) in vec2 v_uv;
    layout(location = 1) in vec4 v_color;
    layout(set = 2, binding = 0) uniform sampler2D atlas;
    layout(location = 0) out vec4 result;
    void main() { result = texture(atlas, v_uv) * v_color; }
    """;

// --- gameplay checks, with no window -----------------------------------------

fn world_checksum() -> u32 {
    var h: u32 = 2166136261;
    for b in blocks { h = (h ^ b) * 16777619; }
    h
}
fn gameplay_tests() {
    generate(123);
    let checksum = world_checksum();
    assert(!blocked(game.player.feet));
    assert(valid_save(game, blocks));
    assert(daylight() == 1.0 && sun_direction().y > 0.0);
    day_time = 450.0;
    assert(daylight() == 0.0 && sun_direction().y < 0.0);
    day_time = DAY_LENGTH - 0.25;
    advance_time(0.5);
    assert(abs(day_time - 0.25) < 0.001);
    generate(123);
    assert(checksum == world_checksum());
    var counts: i64[13] = [0; 13];
    for b in blocks { counts[b]++; }
    assert(counts[WOOD] > 20 && counts[WATER] > 20 && counts[COAL] > 20);
    generate(321);
    assert(checksum != world_checksum());

    // A small, controlled arena isolates movement and interaction from the
    // terrain generator. All checks call the same functions as the game.
    game = Game { version: 1, seed: 7, .. };
    for i in VOLUME { blocks[i] = AIR; }
    for z in DEPTH {
        for x in WIDTH { put(cell(x, 0, z), BEDROCK); }
    }
    game.player = Player { feet: float3 { 10.5, 1.001, 10.5 }, .. };
    put(cell(10, 2, 7), WOOD);
    let hit = ray(eye(), float3 { 0.0, 0.0, -1.0 }, 6.0);
    assert(hit.found && hit.at == cell(10, 2, 7) && hit.before == cell(10, 2, 8));
    assert(!ray(eye(), float3 { 1.0, 0.0, 0.0 }, 6.0).found);
    assert(mine(hit.at) && game.bag[2] == 1 && block_at(hit.at) == AIR);
    assert(debris[0].life > 0.0);
    animate_debris(1.0);
    for bit in debris { assert(bit.life <= 0.0); }
    assert(!mine(cell(10, 0, 10)));
    assert(craft(0) && game.bag[3] == 4 && game.bag[2] == 0);
    assert(!craft(1));
    game.bag[2] = 1;
    assert(craft(0) && craft(1) && game.tool == 1 && game.bag[3] == 3);
    for x in 3 { put(cell(15 + x, 1, 10), STONE); assert(mine(cell(15 + x, 1, 10))); }
    assert(craft(2) && game.tool == 2 && game.bag[1] == 0 && game.bag[3] == 1);
    put(cell(15, 1, 10), COAL);
    assert(mine(cell(15, 1, 10)) && craft(3) && game.bag[6] == 4);
    game.selected = 6;
    assert(place(hit) && block_at(hit.before) == LAMP && lamp_count == 1);
    game.selected = 0;
    pick_material(Hit { true, hit.before, hit.at, 1.0 });
    assert(game.selected == 6 && game.bag[6] == 3);
    assert(!place(hit));                // Cannot place twice in the same cell.
    let self_hit = Hit { true, cell(10, 1, 9), cell(10, 1, 10), 1.0 };
    assert(!place(self_hit));               // Cannot trap the player in a block.
    assert(mine(hit.before) && lamp_count == 0);
    for i in 120 { walk(float2_0, false, false); }
    assert(game.player.grounded && abs(game.player.feet.y - 1.0) < 0.005);
    var peak: f32 = 0.0;
    for i in 180 {
        walk(float2_0, i == 0, false);
        peak = max(peak, game.player.feet.y);
    }
    assert(peak > 2.1 && game.player.grounded);
    for y in 1..5 { put(cell(11, y, 10), STONE); }
    for i in 120 { walk(float2 { 1.0, 0.0 }, false, true); }
    assert(game.player.feet.x < 10.711 && !blocked(game.player.feet));
    assert(blocked(float3 { 0.0, 1.0, 2.0 }));

    // A low ceiling stops a jump; swimming can climb a one-block shore.
    put(cell(10, 3, 10), STONE);
    for i in 40 { walk(float2_0, i == 0, false); }
    assert(game.player.feet.y < 1.201 && !blocked(game.player.feet));
    game.player = Player { feet: float3 { 30.5, 1.0, 30.5 }, .. };
    for y in 1..4 {
        for x in 29..32 { put(cell(x, y, 30), WATER); }
        put(cell(32, y, 30), SAND);
    }
    put(cell(32, 4, 30), SAND);
    for i in 240 { walk(float2 { 1.0, 0.0 }, true, false); }
    assert(game.player.feet.x > 32.5 && !blocked(game.player.feet));
    game.player = Player { feet: float3 { 30.5, 1.0, 30.5 }, .. };
    for i in 240 { walk(float2 { 1.0, 0.0 }, true, false, true); }
    assert(game.player.feet.x > 32.5 && !blocked(game.player.feet));

    // Sneaking holds every edge and corner of a one-block platform, even
    // if run is held too. Jumping or releasing sneak still lets us leave.
    put(cell(60, 3, 60), STONE);
    let directions: float2[8] = [
        float2 { 1.0, 0.0 }, float2 { -1.0, 0.0 },
        float2 { 0.0, 1.0 }, float2 { 0.0, -1.0 },
        float2 { 1.0, 1.0 }, float2 { -1.0, 1.0 },
        float2 { 1.0, -1.0 }, float2 { -1.0, -1.0 } ];
    for direction in directions {
        game.player = Player { feet: float3 { 60.5, 4.001, 60.5 }, grounded: true, .. };
        for i in 240 { walk(direction, false, true, true); }
        assert(game.player.feet.y > 3.999 && game.player.grounded && supported(game.player.feet));
    }
    for i in 120 { walk(float2 { -1.0, -1.0 }, false, false); }
    assert(game.player.feet.y < 3.0);
    game.player = Player { feet: float3 { 60.5, 4.001, 60.5 }, grounded: true, .. };
    for i in 120 { walk(float2 { 1.0, 0.0 }, i == 0, false, true); }
    assert(game.player.feet.x > 61.3 && game.player.feet.y < 3.95);
    game.player = Player { feet: float3 { 60.5, 4.001, 60.5 }, grounded: true, .. };
    put(cell(60, 3, 60), AIR);
    for i in 120 { walk(float2_0, false, false, true); }
    assert(game.player.feet.y < 1.01);       // Mining the floor still makes us fall.

    // Bulk crafting consumes only complete recipes, respects stack space,
    // and buys a permanent tool once. Ordinary crafting still makes one batch.
    let saved_inventory = game.bag;
    let saved_tool = game.tool;
    game.bag[2] = 3;
    game.bag[3] = 0;
    assert(craft_batches(0, false) == 1 && game.bag[2] == 2 && game.bag[3] == 4);
    assert(craft_batches(0, true) == 2 && game.bag[2] == 0 && game.bag[3] == 12);
    game.bag[2] = 3;
    game.bag[3] = 994;
    assert(craft_batches(0, true) == 1 && game.bag[2] == 2 && game.bag[3] == 998);
    assert(craft_batches(0, true) == 0 && game.bag[2] == 2);
    game.bag[7] = 3;
    game.bag[3] = 2;
    game.bag[6] = 0;
    assert(craft_batches(3, true) == 2 && game.bag[7] == 1 && game.bag[3] == 0 && game.bag[6] == 8);
    game.tool = 0;
    game.bag[3] = 10;
    assert(craft_batches(1, true) == 1 && game.tool == 1 && game.bag[3] == 5);
    assert(craft_batches(1, true) == 0 && game.bag[3] == 5);
    game.bag[1] = 6;
    assert(craft_batches(2, true) == 1 && game.tool == 2 && game.bag[1] == 3 && game.bag[3] == 3);
    game.bag = saved_inventory;
    game.tool = saved_tool;

    // Out-of-range reach, negative directions, and chunk-border edits.
    put(cell(30, 8, 30), STONE);
    assert(!ray(float3 { 20.5, 8.5, 30.5 }, float3 { 1.0, 0.0, 0.0 }, 6.0).found);
    let back = ray(float3 { 33.5, 8.5, 30.5 }, float3 { -1.0, 0.0, 0.0 }, 6.0);
    assert(back.found && back.before == cell(31, 8, 30));
    for i in meshes.len { meshes[i].dirty = false; }
    edit(cell(15, 8, 15), PLANKS);
    assert(meshes[0].dirty && meshes[1].dirty && meshes[CHUNKS_X].dirty && meshes[CHUNKS_X + 1].dirty);

    game.selected = 6;
    game.bag[6] = 20;
    for i in MAX_LAMPS { edit(cell(i + 1, 2, 1), LAMP); }
    assert(lamp_count == MAX_LAMPS && !place(back));
    assert(game.bag[6] == 20);
    game.bag[2] = 999;
    put(cell(20, 2, 20), WOOD);
    assert(!mine(cell(20, 2, 20)) && block_at(cell(20, 2, 20)) == WOOD);

    day_time = 450.0;
    var image = save_image();
    let saved_checksum = world_checksum();
    let saved_bag = game.bag;
    blocks[0] = AIR;
    game.bag[0] = 123;
    day_time = 90.0;
    assert(restore(image) && valid_save(game, blocks));
    assert(world_checksum() == saved_checksum && game.bag == saved_bag);
    assert(day_time == 450.0);
    // Non-finite/out-of-range clocks cannot replace a good world.
    day_time = -1.0;
    let bad_clock = save_image();
    day_time = 450.0;
    assert(!restore(bad_clock) && day_time == 450.0);
    let grid_start = 4 + to_bytes([Game { .. }]).len;
    var legacy: u8[>..] = [];
    legacy.append(image[..grid_start + VOLUME]);
    legacy[3] = '1';
    assert(restore(legacy) && day_time == 90.0);
    assert(world_checksum() == saved_checksum && game.bag == saved_bag);
    assert(!restore(image[..image.len - 1]));
    assert(!restore(image[..2]));
    image[0] = 'X';
    assert(!restore(image) && world_checksum() == saved_checksum);
    image[0] = 'G';
    image[grid_start] = AIR;
    assert(!restore(image));
    game.selected = 100;
    assert(!valid_save(game, blocks));
    print("mini minecraft: terrain, ray, mining, crafting, placement, movement, daylight and saves passed");
}

// --- the program: input, simulation, mesh updates, and drawing ---------------

fn main() {
    var seed: u32 = 2026;
    var frames = -1;
    var test = false;
    let argv = args();
    var a = 1;
    while a < argv.len {
        if argv[a] == "--test" { test = true; a++; continue; }
        if a + 1 >= argv.len || (argv[a] != "--seed" && argv[a] != "--frames") {
            write_stderr("usage: mini_minecraft [--seed N] [--frames N] [--test]\n");
            exit(1);
        }
        let n, ok = parse_int(argv[a + 1]);
        if !ok || n < 0 || n > 2147483647 || (argv[a] == "--frames" && n == 0) {
            write_stderr("seed must be 0..2147483647; frames must be 1..2147483647\n");
            exit(1);
        }
        if argv[a] == "--seed" { seed = n as u32; } else { frames = n; }
        a += 2;
    }
    if test {
        gameplay_tests();
        if frames < 0 { return; }
    }
    generate(seed);
    guard gfx::open("Goose: mini Minecraft", 1280, 800, gfx::WINDOW_RESIZABLE) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let atlas = make_atlas();
    let sampler = gfx::sampler(gfx::NEAREST, gfx::CLAMP);
    let world_pipeline = gfx::pipeline(embed_shader("vert", world_vertex),
        embed_shader("frag", scene_uniform, world_fragment),
        gfx::PipelineDesc { cull: gfx::CULL_BACK, depth_test: true, depth_write: true });
    let sky_pipeline = gfx::pipeline(embed_shader("vert", sky_vertex), embed_shader("frag", sky_fragment));
    var hud_desc = gfx::PipelineDesc { blend: gfx::BLEND_ALPHA };
    hud_desc.formats[2] = gfx::VERTEX_UBYTE4_NORM;
    let hud_pipeline = gfx::pipeline(embed_shader("vert", hud_vertex),
                                     embed_shader("frag", hud_fragment), hud_desc);
    if sampler.id == 0 || world_pipeline.id == 0 || sky_pipeline.id == 0 || hud_pipeline.id == 0 {
        abort(str("renderer: ", gfx::error()));
    }
    rebuild_dirty();
    var hud_buffer = gfx::Buffer { 0 };
    // This small fixed array sets the buffer's capacity once. Each frame
    // uploads only the live fragments; the GPU buffer is reused throughout.
    let empty_debris: Vertex[MAX_DEBRIS * 6] = [Vertex { .. }; MAX_DEBRIS * 6];
    let debris_buffer = gfx::vertex_buffer(bytes_of(empty_debris));
    if debris_buffer.id == 0 { abort(str("fragment buffer: ", gfx::error())); }
    var crafting = false;
    var paused = !gfx::focused();
    var help = true;
    var drawn = 0;
    var elapsed: f32 = 0.0;
    var accumulator: f32 = 0.0;
    var progress: f32 = 0.0;
    var mining_at = cell(-1, -1, -1);
    var place_delay: f32 = 0.0;
    var wait_for_buttons = false;
    let keys: const u8[:][SLOTS] = ["1", "2", "3", "4", "5", "6", "7", "8"];
    if !paused && !gfx::set_mouse_relative(true) { say("MOUSE CAPTURE FAILED - USE ARROW KEYS TO LOOK"); }

    while gfx::frame() {
        let pixels = gfx::screen_size();
        if pixels.x <= 0 || pixels.y <= 0 {
            paused = true;
            crafting = false;
            progress = 0.0;
            accumulator = 0.0;
            gfx::set_mouse_relative(false);
            continue;
        }
        // A logical canvas keeps the HUD legible on high-density screens.
        let scale = max(0.5, min<f32>(pixels.x / 960.0, pixels.y / 600.0));
        let screen = float2 { pixels.x / scale, pixels.y / scale };
        let mouse = gfx::mouse_pos() / float2 { scale, scale };
        let dt: f32 = if frames > 0 { 1.0 / 60.0 } else { min(gfx::delta_time(), 0.05) as f32 };
        message_time = max(0.0, message_time - dt);
        let was_playing = !paused && !crafting;
        let was_crafting = crafting;
        // A loss followed by a gain in the same frame must still pause.
        // gfx clears held input and releases capture on every focus loss.
        var lost_focus = !gfx::focused();
        for event in gfx::events() {
            if event.kind == gfx::EVENT_FOCUS_LOST { lost_focus = true; }
        }
        if lost_focus { paused = true; crafting = false; }
        else {
            if gfx::key_pressed("H") { help = !help; }
            if gfx::key_pressed("Escape") {
                if crafting { crafting = false; } else { paused = !paused; }
            } else if paused && gfx::mouse_pressed(gfx::MOUSE_LEFT) { paused = false; }
            else if gfx::key_pressed("C") && !paused { crafting = !crafting; }
            if gfx::key_pressed("F5") { save_game(); }
            if gfx::key_pressed("F9") { load_game(); progress = 0.0; accumulator = 0.0; }
        }
        let playing = !paused && !crafting;
        if playing != was_playing {
            // The default visible cursor returns in menus. Changing capture
            // also discards stale motion, so reopening the game cannot jump.
            if !gfx::set_mouse_relative(playing) && playing {
                say("MOUSE CAPTURE FAILED - USE ARROW KEYS TO LOOK");
            }
            wait_for_buttons = true;
        }
        // A resume/menu click must be released before it can mine or build.
        if !gfx::mouse_down(gfx::MOUSE_LEFT) && !gfx::mouse_down(gfx::MOUSE_RIGHT) {
            wait_for_buttons = false;
        }

        if crafting && was_crafting {
            let x = (screen.x - 504.0) * 0.5;
            let y = (screen.y - 300.0) * 0.5 + 78.0;
            for i in 4 {
                let row_y = y + i * 46.0;
                let clicked = gfx::mouse_pressed(gfx::MOUSE_LEFT) && mouse.x >= x + 16.0 &&
                    mouse.x < x + 488.0 && mouse.y >= row_y && mouse.y < row_y + 40.0;
                if clicked || gfx::key_pressed(keys[i]) {
                    craft_batches(i, gfx::key_down("Left Shift") || gfx::key_down("Right Shift"));
                }
            }
        }
        if playing && was_playing {
            elapsed += dt;
            advance_time(dt);
            animate_debris(dt);
            for i in SLOTS { if gfx::key_pressed(keys[i]) { game.selected = i as i32; } }
            let wheel = gfx::mouse_wheel();
            if wheel != 0.0 {
                let step = if wheel > 0.0 { -1 } else { 1 };
                game.selected = ((game.selected + step + SLOTS) % SLOTS) as i32;
            }
            if gfx::mouse_relative() {
                let delta = gfx::mouse_delta();
                game.player.yaw += delta.x * 0.003;
                game.player.pitch -= delta.y * 0.003;
            }
            if gfx::key_down("Left") { game.player.yaw -= dt * 1.8; }
            if gfx::key_down("Right") { game.player.yaw += dt * 1.8; }
            if gfx::key_down("Up") { game.player.pitch += dt * 1.5; }
            if gfx::key_down("Down") { game.player.pitch -= dt * 1.5; }
            game.player.pitch = clamp(game.player.pitch, -1.5, 1.5);
            game.player.yaw = (game.player.yaw + PI) % TAU - PI;

            var direction = float2_0;
            if gfx::key_down("W") { direction.y += 1.0; }
            if gfx::key_down("S") { direction.y -= 1.0; }
            if gfx::key_down("D") { direction.x += 1.0; }
            if gfx::key_down("A") { direction.x -= 1.0; }
            accumulator += dt;
            let sneak = gfx::key_down("Left Ctrl") || gfx::key_down("Right Ctrl");
            while accumulator >= STEP {
                walk(direction, gfx::key_down("Space"), gfx::key_down("Left Shift"), sneak);
                accumulator -= STEP;
            }
        } else { accumulator = 0.0; }

        var hit = ray(eye(), forward(), 6.0);
        if playing && was_playing && !wait_for_buttons {
            if gfx::mouse_pressed(gfx::MOUSE_MIDDLE) { pick_material(hit); }
            if hit.found && gfx::mouse_down(gfx::MOUSE_LEFT) {
                if hit.at != mining_at { progress = 0.0; mining_at = hit.at; }
                let seconds = mining_seconds(block_at(hit.at));
                if seconds > 0.0 { progress += dt / seconds; } else { progress = 0.0; }
                if progress >= 1.0 {
                    mine(hit.at);
                    progress = 0.0;
                    hit = ray(eye(), forward(), 6.0);
                }
            } else { progress = 0.0; mining_at = cell(-1, -1, -1); }
            // Edges include a press and release between frames. Held input
            // repeats slowly enough to build a wall without emptying a stack.
            place_delay = max(0.0, place_delay - dt);
            if gfx::key_pressed("E") || gfx::mouse_pressed(gfx::MOUSE_RIGHT) ||
               (place_delay == 0.0 && (gfx::key_down("E") || gfx::mouse_down(gfx::MOUSE_RIGHT))) {
                place(hit);
                place_delay = 0.22;
                hit = ray(eye(), forward(), 6.0);
            }
        } else { progress = 0.0; place_delay = 0.0; }
        rebuild_dirty();

        // The frame's temporary HUD array is freed at this iteration's end.
        // Reuse its GPU buffer until growth needs a larger one.
        var hud_vertices: HudVertex[>..] = [];
        hud(hud_vertices, screen, hit, progress, crafting, paused, help);
        let hud_bytes = bytes_of(hud_vertices);
        if hud_buffer.id == 0 || gfx::buffer_size(hud_buffer) < hud_bytes.len {
            if hud_buffer.id != 0 { gfx::release(hud_buffer); }
            hud_buffer = gfx::buffer(gfx::BUFFER_VERTEX, hud_bytes.len * 2, hud_bytes);
            if hud_buffer.id == 0 { abort(str("HUD buffer: ", gfx::error())); }
        } else if !gfx::update_buffer(hud_buffer, hud_bytes) { abort(str("HUD upload: ", gfx::error())); }

        let ahead = forward();
        let right = normalize(cross(ahead, float3 { 0.0, 1.0, 0.0 }));
        let up = cross(right, ahead);
        let fragments = debris_vertices(right, up);
        if fragments.len > 0 && !gfx::update_buffer(debris_buffer, bytes_of(fragments)) {
            abort(str("fragment upload: ", gfx::error()));
        }
        let aspect = pixels.x as f32 / pixels.y;
        let view = gfx::look_at(eye(), eye() + ahead, float3 { 0.0, 1.0, 0.0 });
        let view_proj = gfx::mul(gfx::perspective(1.15, aspect, 0.05, 150.0), view);
        let underwater = block_at(floor_cell(eye())) == WATER;
        let sky_color = horizon();
        let haze = if underwater { float4 { 0.08, 0.28, 0.38, 1.0 } }
                   else { float4 { sky_color.x, sky_color.y, sky_color.z, 0.0 } };
        let sun_dir = sun_direction();
        let sun = float4 { sun_dir.x, sun_dir.y, sun_dir.z, daylight() };
        let target = if hit.found && !crafting && !paused {
            float4 { hit.at.x, hit.at.y, hit.at.z, progress }
        } else { float4 { 0.0, 0.0, 0.0, -1.0 } };
        let camera = eye();
        let scene = Scene { view_proj, float4 { camera.x, camera.y, camera.z, elapsed },
                            target, lamps, haze, sun };
        let lens = tan(1.15 as f32 * 0.5);
        let sky = Sky {
            float4 { ahead.x / lens, ahead.y / lens, ahead.z / lens, 0.0 },
            float4 { right.x, right.y, right.z, 0.0 },
            float4 { up.x, up.y, up.z, 0.0 },
            float4 { aspect, elapsed, if underwater { 1.0 } else { 0.0 }, 0.0 }, sun, haze };

        gfx::begin_screen(haze);
        gfx::bind(sky_pipeline);
        gfx::uniforms(gfx::FRAGMENT, 0, sky);
        gfx::draw(3);
        gfx::bind(world_pipeline);
        gfx::bind_texture(gfx::FRAGMENT, 0, atlas, sampler);
        gfx::uniforms(gfx::VERTEX, 0, view_proj);
        gfx::uniforms(gfx::FRAGMENT, 0, scene);
        for m in meshes {
            if m.count == 0 { continue; }
            gfx::bind_vertex_buffer(m.vertices);
            gfx::bind_index_buffer(m.indices, 4);
            gfx::draw_indexed(m.count);
        }
        if fragments.len > 0 {
            gfx::bind_vertex_buffer(debris_buffer);
            gfx::draw(fragments.len);
        }
        gfx::bind(hud_pipeline);
        gfx::bind_texture(gfx::FRAGMENT, 0, atlas, sampler);
        gfx::bind_vertex_buffer(hud_buffer);
        gfx::uniforms(gfx::VERTEX, 0, float4 { screen.x, screen.y, 0.0, 0.0 });
        gfx::draw(hud_vertices.len);
        gfx::end_pass();
        if gfx::key_pressed("F12") {
            if gfx::screenshot("mini_minecraft.png") { say("SAVED MINI_MINECRAFT.PNG"); }
            else { say("SCREENSHOT FAILED"); }
        }
        drawn++;
        if drawn == frames { break; }
    }
    if frames > 0 {
        let saved = gfx::screenshot("mini_minecraft.png");
        print("drew ", drawn, " frames; saved mini_minecraft.png: ", saved);
    }
    // close releases every remaining GPU object, including chunk buffers.
    gfx::close();
}
