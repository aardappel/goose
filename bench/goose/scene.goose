// A scene graph: a tree of nodes with a local transform each, whose world
// transforms are recomputed top-down every frame after some nodes move. It
// is the shape of every game engine's scene, every UI toolkit's widget tree
// and every robot's kinematic chain, and the thing it asks of a language is
// a tree with a variable number of children per node whose payload is
// mutated in place every frame.
//
// A Goose node carries its children inline as a limited array of 4-byte
// relative references (spec 5.3, 3.9), so a node is one 117-byte record, the
// whole scene is one contiguous pool, and a child is reached by adding an
// offset to the address of the slot that names it. C++'s idiomatic shape is
// a vector<unique_ptr<Node>> per node -- an allocation per child plus one
// for the vector -- and the expert one is the flat arena with a fixed child
// array, which is also the shape safe Rust writes.
//
// Every transform is an axis-aligned 90-degree rotation with an integer
// translation, so all the float math is exact and every implementation
// produces a bit-identical checksum.

import rng;

let DEPTH = 13;                          // BENCH_N
let FRAMES = 16;
let MAXKIDS = 4;

struct F3 { x: f32, y: f32, z: f32 }
struct Xf { m: f32[9], t: F3 }
struct SNode { id: i32, local: Xf, world: Xf, kids: (SNode&<u32>)[..MAXKIDS] }

var pool: SNode[>..] = [];

let IDENT = Xf { m: [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0],
                 t: F3 { x: 0.0, y: 0.0, z: 0.0 } };

// One of twelve axis-aligned rotations, from the seed.
fn rotation(seed: u64) -> f32[9] {
    let axis = xs_mod(seed, 3);
    let quarter = xs_mod(seed >> 8, 4);
    var c: f32 = 1.0;
    var s: f32 = 0.0;
    if quarter == 1 { c = 0.0; s = 1.0; }
    else if quarter == 2 { c = -1.0; s = 0.0; }
    else if quarter == 3 { c = 0.0; s = -1.0; }
    if axis == 0 { return [1.0, 0.0, 0.0, 0.0, c, 0.0 - s, 0.0, s, c]; }
    if axis == 1 { return [c, 0.0, s, 0.0, 1.0, 0.0, 0.0 - s, 0.0, c]; }
    return [c, 0.0 - s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0];
}

// The node is pushed before its children so the parent's child links are
// filled in as the children are built; the base case returns first, which
// tells the checker the recursive results are rooted at the pool.
recursive fn build(depth: i64, seed: u64) -> SNode& {
    let r = xs_next(seed);
    let nd: SNode& = pool.push(SNode {
        id: pool.len as i32,
        local: Xf { m: rotation(r), t: F3 { x: xs_mod(r >> 16, 9) as f32,
                                            y: xs_mod(r >> 24, 9) as f32,
                                            z: xs_mod(r >> 32, 9) as f32 } },
        world: IDENT,
        kids: [] });
    if depth == 0 { return nd; }
    let n = 1 + xs_mod(r >> 40, MAXKIDS);
    for i in n {
        let c: SNode& = build(depth - 1, xs_next(r + i as u64));
        nd.kids.push(c);
    }
    return nd;
}

// world = parent ∘ local: a 3x3 product and a transformed translation.
fn compose(p: Xf&, l: Xf&, w: Xf&) {
    for i in 3 {
        for j in 3 {
            var s: f32 = 0.0;
            for k in 3 { s = s + p.m[i * 3 + k] * l.m[k * 3 + j]; }
            w.m[i * 3 + j] = s;
        }
    }
    w.t.x = p.m[0] * l.t.x + p.m[1] * l.t.y + p.m[2] * l.t.z + p.t.x;
    w.t.y = p.m[3] * l.t.x + p.m[4] * l.t.y + p.m[5] * l.t.z + p.t.y;
    w.t.z = p.m[6] * l.t.x + p.m[7] * l.t.y + p.m[8] * l.t.z + p.t.z;
}

recursive fn update(n: SNode&, parent: Xf&) {
    compose(parent, n.local, n.world);
    for i in n.kids.len { update(n.kids[i], n.world); }
}

fn main() {
    let root: SNode& = build(DEPTH, 12345);
    var total = 0;
    for f in FRAMES {
        // A seventh of the nodes move each frame: the animation.
        let step = (f % 5) as f32;
        for &n in pool {
            if n.id % 7 == f % 7 { n.local.t.x = step; n.local.t.z = 8.0 - step; }
        }
        update(root, IDENT);
        // Where everything ended up, as an integer: exact in f32, summed in i64.
        for &n in pool {
            total += (n.world.t.x + n.world.t.y + n.world.t.z) as i64;
        }
    }
    print(pool.len);
    print(total);
}
