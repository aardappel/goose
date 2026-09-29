// A reference bound to a reusable pool carries the pool's freelist (§5.4), and
// keeps carrying one when .= rebinds it to another pool of its kind: a local,
// a global rebound in a function, a `var` parameter, a variable a nested
// function rebinds to a pool it captures, a slice pool, and one alternating
// between two pools in a loop. An allocation after a rebind reuses what the
// new pool freed, never the old pool's. A plain reference, and an optional
// one, may point at a pool too, as at an array.

reusable var gpool: i64[>..] = [10, 11, 12];
reusable var gpool2: i64[>..] = [20, 21, 22];
var gref: i64[>..]& = gpool;
var plain: i64[>..] = [];

fn retarget() { gref .= gpool2; }

fn globals() {
    gpool2.free(1);
    print(gref.alloc_index(13));                                  // 3: gpool has no free slot
    retarget();
    print(gref.alloc_index(23));                                  // 1: gpool2's
    print(gpool.len, " ", gpool2.len, " ", gpool2[1]);            // 4 3 23
}

fn locals() {
    reusable var a: i64[>..] = [1, 2, 3];
    reusable var b: i64[>..] = [4, 5, 6];
    a.free(0);
    b.free(2);
    var r: i64[>..]& = a;
    print(r.alloc_index(7));                                      // 0: a's
    r .= b;
    print(r.alloc_index(8), " ", r.alloc_index(9));               // 2: b's, then 3: b grows
    print(a.len, " ", b.len, " ", b[2], " ", b[3]);               // 3 4 8 9
    let q: i64[>..]& = a;
    a.free(1);
    r .= q;                                                       // A pool reference of the kind.
    print(r.alloc_index(5), " ", a[1]);                           // 1 5
}

// Given a pool, the parameter is a pool reference, rebound to another pool.
fn swap_alloc(var p: i64[>..]&, v: i64) -> i64 {
    let i = p.alloc_index(v);
    p .= gpool;
    p.free(0);
    return i * 100 + p.alloc_index(v + 1);
}

fn nested() {
    reusable var a: i64[>..] = [1, 2];
    reusable var b: i64[>..] = [3, 4];
    b.free(0);
    var r: i64[>..]& = a;
    fn to_b() { r .= b; }
    to_b();
    print(r.alloc_index(9), " ", b[0], " ", a.len);               // 0 9 2
}

fn slices() {
    reusable[] var a: i64[>..] = [];
    reusable[] var b: i64[>..] = [];
    a.push(1);
    let t = b.alloc_slice(3);
    b.free_slice(t);
    var r: i64[>..]& = a;
    r .= b;
    let u = r.alloc_slice(2);                                     // b's freed run
    u[0] = 7;
    u[1] = 8;
    print(a, " ", b);                                             // [1] [7, 8, 0]
    r.free_slice(u);
    print(r.alloc_slice(3).len, " ", b.len);                      // 3 3
}

fn alternate() {
    reusable var a: i64[>..] = [];
    reusable var b: i64[>..] = [];
    var r: i64[>..]& = a;
    for i in 4 {
        r.free(r.alloc_index(i));
        if i % 2 == 0 { r .= b; } else { r .= a; }
    }
    print(a, " ", b);                                             // [2] [3]
}

fn plain_to_pool() {
    reusable var a: i64[>..] = [1];
    var other: i64[>..] = [5];
    var r: i64[>..]& = other;
    r .= a;
    r.push(2);
    var o: i64[>..]&? = null;
    o .= a;
    if o {
        let m .= o;
        m.push(3);
    }
    a.free(0);
    print(a.alloc_index(4), " ", a);                              // 0 [4, 2, 3]
}

fn show_len(o: i64[>..]&?) -> i64 {
    if o { return o.len; }
    return -1;
}
fn pass_on(o: i64[>..]&?) -> i64 { show_len(o) }

// Nothing calls this: checked standalone, its parameter may be given a pool or
// not, and rebinding it to an array that is not one settles that it is not.
fn unreached(var p: i64[>..]&) {
    p .= plain;
    p.push(1);
}

fn main() {
    globals();
    locals();
    print(swap_alloc(gpool2, 30));                                // 300 + 0 = 300
    print(gpool[0], " ", gpool2[3]);                              // 31 30
    nested();
    slices();
    alternate();
    plain_to_pool();
    print(pass_on(gpool), " ", pass_on(null));                    // 4 -1
}
