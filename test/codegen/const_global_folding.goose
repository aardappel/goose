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

// Listed elements are static data at any length; a fill past 256 elements
// runs its initializer, a loop, rather than being spelled out.
const LONG: i64[260] = [
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
    60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
    100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139,
    140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199,
    200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219,
    220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259
];
const FILLED: i64[300] = [3; 300];

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
    print(LONG[259], " ", LONG[17], " ", FILLED[299]);
}
