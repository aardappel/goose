// A scene graph: a tree of nodes with a local transform each, whose world
// transforms are recomputed top-down every frame after some nodes move. See
// bench/goose/scene.goose.
//
// This is the owning-pointer shape: every node holds its children as
// `Vec<Box<SNode>>`, so a node costs one allocation plus one for its child
// vector, the tree is scattered across the heap, and the whole thing is taken
// apart by a recursive `Drop` at exit. It is what a Rust programmer writes
// first for a tree whose children are a variable-length list, and unlike the
// arena row it lets the update pass hold a real `&Xf` to the parent's world
// transform while mutating the child.
//
// Every transform is an axis-aligned 90-degree rotation with an integer
// translation, so all the float math is exact and the checksum is bit-identical
// across languages.
mod bench;
use bench::*;

const DEPTH: i64 = 12;                   // BENCH_N
const FRAMES: i64 = 16;
const MAXKIDS: i64 = 4;

#[derive(Clone, Copy)]
struct F3 { x: f32, y: f32, z: f32 }
#[derive(Clone, Copy)]
struct Xf { m: [f32; 9], t: F3 }

struct SNode { id: i32, local: Xf, world: Xf, kids: Vec<Box<SNode>> }

const IDENT: Xf = Xf { m: [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0],
                       t: F3 { x: 0.0, y: 0.0, z: 0.0 } };

// One of twelve axis-aligned rotations, from the seed.
fn rotation(seed: u64) -> [f32; 9] {
    let axis = xs_mod(seed, 3);
    let quarter = xs_mod(seed >> 8, 4);
    let mut c: f32 = 1.0;
    let mut s: f32 = 0.0;
    if quarter == 1 { c = 0.0; s = 1.0; }
    else if quarter == 2 { c = -1.0; s = 0.0; }
    else if quarter == 3 { c = 0.0; s = -1.0; }
    if axis == 0 { return [1.0, 0.0, 0.0, 0.0, c, 0.0 - s, 0.0, s, c]; }
    if axis == 1 { return [c, 0.0, s, 0.0, 1.0, 0.0, 0.0 - s, 0.0, c]; }
    [c, 0.0 - s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0]
}

// The node's id is its creation index, assigned before its children are built,
// so the ids run in pre-order exactly as the pool positions do in the arena row.
fn build(count: &mut i64, depth: i64, seed: u64) -> Box<SNode> {
    let r = xs_next(seed);
    let id = *count as i32;
    *count += 1;
    let mut nd = Box::new(SNode {
        id,
        local: Xf { m: rotation(r), t: F3 { x: xs_mod(r >> 16, 9) as f32,
                                            y: xs_mod(r >> 24, 9) as f32,
                                            z: xs_mod(r >> 32, 9) as f32 } },
        world: IDENT,
        kids: Vec::new() });
    if depth == 0 { return nd; }
    let n = 1 + xs_mod(r >> 40, MAXKIDS);
    for i in 0..n {
        nd.kids.push(build(count, depth - 1, xs_next(r.wrapping_add(i as u64))));
    }
    nd
}

// world = parent o local: a 3x3 product and a transformed translation.
fn compose(p: &Xf, l: &Xf, w: &mut Xf) {
    for i in 0..3 {
        for j in 0..3 {
            let mut s: f32 = 0.0;
            for k in 0..3 { s = s + p.m[i * 3 + k] * l.m[k * 3 + j]; }
            w.m[i * 3 + j] = s;
        }
    }
    w.t.x = p.m[0] * l.t.x + p.m[1] * l.t.y + p.m[2] * l.t.z + p.t.x;
    w.t.y = p.m[3] * l.t.x + p.m[4] * l.t.y + p.m[5] * l.t.z + p.t.y;
    w.t.z = p.m[6] * l.t.x + p.m[7] * l.t.y + p.m[8] * l.t.z + p.t.z;
}

// The parent's world transform is borrowed in place: `n.kids` and `n.world` are
// disjoint fields, so the child may be mutated while the parent is read.
fn update(n: &mut SNode, parent: &Xf) {
    compose(parent, &n.local, &mut n.world);
    for k in &mut n.kids { update(k, &n.world); }
}

// A seventh of the nodes move each frame: the animation.
fn animate(n: &mut SNode, f: i64, step: f32) {
    if n.id as i64 % 7 == f % 7 { n.local.t.x = step; n.local.t.z = 8.0 - step; }
    for k in &mut n.kids { animate(k, f, step); }
}

// Where everything ended up, as an integer: exact in f32, summed in i64.
fn sum_world(n: &SNode) -> i64 {
    let mut t = (n.world.t.x + n.world.t.y + n.world.t.z) as i64;
    for k in &n.kids { t += sum_world(k); }
    t
}

fn main() {
    let mut count: i64 = 0;
    let mut root = build(&mut count, DEPTH, 12345);
    let mut total: i64 = 0;
    for f in 0..FRAMES {
        let step = (f % 5) as f32;
        animate(&mut root, f, step);
        update(&mut root, &IDENT);
        total += sum_world(&root);
    }
    emit(count);
    emit(total);
}
