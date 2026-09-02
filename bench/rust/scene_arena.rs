// The same scene graph in one flat arena: a `Vec<SNode>` where each node names
// its children by `u32` index in a fixed four-element array, which is the Goose
// layout and the shape the Rust community recommends for anything pointer-heavy.
// One allocation for the whole scene, no per-node child vector, and no teardown
// walk.
//
// What it costs is that the update pass can no longer hold a reference to the
// parent's world transform while writing the child's: `&pool[p].world` and
// `&mut pool[c].world` are two borrows of the same `Vec` and neither `Index` nor
// `IndexMut` can split them. The composed transform is therefore built in a
// stack local and stored into the pool afterwards, and the recursion passes a
// reference to that local down -- one extra 48-byte copy per node per frame that
// the Goose row, which composes straight into the pool through an `Xf&`, does
// not pay. The child list has to be copied out of the node for the same reason.
mod bench;
use bench::*;

const DEPTH: i64 = 12;                   // BENCH_N
const FRAMES: i64 = 16;
const MAXKIDS: i64 = 4;

#[derive(Clone, Copy)]
struct F3 { x: f32, y: f32, z: f32 }
#[derive(Clone, Copy)]
struct Xf { m: [f32; 9], t: F3 }

struct SNode { id: i32, local: Xf, world: Xf, kids: [u32; 4], nkids: u8 }

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

// The node is pushed before its children, so its index is its pre-order id.
fn build(pool: &mut Vec<SNode>, depth: i64, seed: u64) -> u32 {
    let r = xs_next(seed);
    let me = pool.len() as u32;
    pool.push(SNode {
        id: me as i32,
        local: Xf { m: rotation(r), t: F3 { x: xs_mod(r >> 16, 9) as f32,
                                            y: xs_mod(r >> 24, 9) as f32,
                                            z: xs_mod(r >> 32, 9) as f32 } },
        world: IDENT,
        kids: [0; 4], nkids: 0 });
    if depth == 0 { return me; }
    let n = 1 + xs_mod(r >> 40, MAXKIDS);
    for i in 0..n {
        let c = build(pool, depth - 1, xs_next(r.wrapping_add(i as u64)));
        let nd = &mut pool[me as usize];
        nd.kids[nd.nkids as usize] = c;
        nd.nkids += 1;
    }
    me
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

fn update(pool: &mut Vec<SNode>, i: usize, parent: &Xf) {
    let mut w = IDENT;
    compose(parent, &pool[i].local, &mut w);
    pool[i].world = w;
    let kids = pool[i].kids;
    let nkids = pool[i].nkids as usize;
    for j in 0..nkids { update(pool, kids[j] as usize, &w); }
}

fn main() {
    let mut pool: Vec<SNode> = Vec::new();
    let root = build(&mut pool, DEPTH, 12345);
    let mut total: i64 = 0;
    for f in 0..FRAMES {
        // A seventh of the nodes move each frame: the animation.
        let step = (f % 5) as f32;
        for n in pool.iter_mut() {
            if n.id as i64 % 7 == f % 7 { n.local.t.x = step; n.local.t.z = 8.0 - step; }
        }
        update(&mut pool, root as usize, &IDENT);
        // Where everything ended up, as an integer: exact in f32, summed in i64.
        for n in pool.iter() { total += (n.world.t.x + n.world.t.y + n.world.t.z) as i64; }
    }
    emit(pool.len() as i64);
    emit(total);
}
