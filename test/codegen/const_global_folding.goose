// Constant globals whose initializers name other constant globals (§11.1)
// are static data like literal ones: the named values are spelled out in
// the initializer, through struct fields, array elements and fills, at an
// integer constant's narrower type, and across a chain of such globals. A
// `var` initialized from one is its own copy, and a name the static form
// cannot spell (an element of another constant) leaves the global to run
// its initializer, still in declaration order.

struct Color { r: f64, g: f64, b: f64 }
struct Sphere { pos: f64[3], radius: f64, color: Color, tag: u8 }

const RED = Color { 1.0, 0.0, 0.0 };
const GREEN = Color { 0.0, 1.0, 0.0 };
const WHITE = Color { 1.0, 1.0, 1.0 };
const TAG = 7;
const BIG: i64 = 1 << 40;
const NEG: i64 = -5;
const HUGE: u64 = 18446744073709551615;
const HALF: f32 = 0.5;

const SCENE: Sphere[2] = [
    Sphere { pos: [-1.0, 0.0, 3.0], radius: 0.3, color: RED, tag: TAG },
    Sphere { pos: [0.0, 0.0, 3.0], radius: 0.8, color: GREEN, tag: TAG },
];
const PALETTE: Color[3] = [RED, GREEN, WHITE];
const GREENS: Color[4] = [GREEN; 4];
const FIRST = SCENE;                       // A chain of two names.
const AGAIN = FIRST;
const NUMS: i64[4] = [BIG, NEG, TAG, BIG];
const WIDE: u64[2] = [HUGE, TAG];
const HALVES: f32[2] = [HALF, HALF];
const PICKED = PALETTE[2];                 // Not a name: runs its initializer.
const AFTER = Sphere { pos: [1.0, 1.0, 1.0], radius: 1.0, color: PICKED, tag: 1 };

var paint = RED;

fn total(s: Sphere[2]) -> f64 {
    var t = 0.0;
    for x in s { t += x.pos[0] + x.pos[2] + x.radius + x.color.r + x.color.g + x.tag; }
    return t;
}

fn main() {
    print(total(SCENE), " ", total(AGAIN), " ", SCENE[1].color.g, " ", SCENE[0].tag);
    print(PALETTE[0].r, " ", PALETTE[1].g, " ", PALETTE[2].b, " ", GREENS[3].g);
    print(NUMS, " ", WIDE, " ", HALVES);
    print(PICKED.r, " ", AFTER.color.b, " ", AFTER.tag);
    paint.r = 0.25;
    print(paint.r, " ", RED.r, " ", SCENE[0].color.r);
    var sum = 0.0;
    for c in PALETTE { sum += c.r + c.g + c.b; }
    print(sum);
}
