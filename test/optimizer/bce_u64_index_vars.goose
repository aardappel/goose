// A u64 of an index type is a base, being below its array's length, so a
// write of one is a kill as for any other integer variable: an assignment,
// a callee's write through a reference to the variable or of a global one,
// and a write through a reference that may name a global one. The bound a
// check proved before the write no longer holds after it.
import std;

var nodes: i32[>..] = [0, 1, 2, 3, 4, 5, 6, 7];
type BigId = u64 in nodes;
var gw: BigId = 0 as BigId;

fn assigned(small: i32[>..]&, a: u32[>..]&) -> i32 {
    var w = a[0] as BigId;
    var t = small[w];                // bce:keep
    w = a[1] as BigId;
    t += small[w];                   // bce:keep
    return t;
}

// Recursive, so never inlined: a caller sees the effects alone.
recursive fn reset(p: BigId&, a: u32[>..]&, k: i64) {
    if k > 0 { reset(p, a, k - 1); return; }
    p = a[1] as BigId;
}

fn by_reference(small: i32[>..]&, a: u32[>..]&) -> i32 {
    var w = a[0] as BigId;
    var t = small[w];                // bce:keep
    reset(w, a, 1);
    t += small[w];                   // bce:keep
    return t;
}

recursive fn set_global(a: u32[>..]&, k: i64) {
    if k > 0 { set_global(a, k - 1); return; }
    gw = a[1] as BigId;
}

fn global(small: i32[>..]&, a: u32[>..]&) -> i32 {
    gw = a[0] as BigId;
    var t = small[gw];               // bce:keep
    set_global(a, 1);
    t += small[gw];                  // bce:keep
    return t;
}

recursive fn through(p: BigId&, small: i32[>..]&, a: u32[>..]&, k: i64) -> i32 {
    if k > 0 { return through(p, small, a, k - 1); }
    var t = small[gw];               // bce:keep
    p = a[1] as BigId;
    t += small[gw];                  // bce:keep
    return t;
}

fn main() {
    var small: i32[>..] = [5];
    var ok: u32[>..] = [0, 0];
    var bad: u32[>..] = [0, 7];
    print(assigned(small, ok), " ", by_reference(small, ok), " ", global(small, ok), " ",
          through(gw, small, ok, 1));
    print(assigned(small, bad));
}
