// runtime-debug: a chunk handed to a callee that resizes the elements it
// views is a slice of its receiver's elements, as a slice of the receiver
// would be (BCE::KillSliceArg): what a reference into one of those elements
// measured is gone after the call, and what one into another array's
// element measured is not.
recursive fn pop_first(p: i64[..4][:]) { p[0].pop(); p[0].pop(); }

fn main() {
    var a: i64[..4][>..] = [];
    var b: i64[..4][>..] = [];
    var x: i64[..4]      = [1, 2, 3];
    a.push(x);
    a.push(x);
    a.push(x);
    b.push(x);
    let u = &b[0];
    let m = u.len;
    let w = &a[0];
    let n = w.len;
    pop_first(a.chunk(0, 3));
    if n > 0 { print(w[0]); } // bce:keep
    if m > 2 { print(u[2]); } // bce:elide
    let v = &a[1];
    let nv = v.len;
    let bounds: i64[2] = [1, 2];
    pop_first(a.chunk_at(bounds, 0));
    if nv > 0 { print(v[0]); } // bce:keep
    if m > 2 { print(u[2]); } // bce:elide
    // A chunk of a chunk views the elements of the first one's receiver.
    let t = &a[2];
    let nt = t.len;
    pop_first(a.chunk(0, 1).chunk(1, 2));
    if nt > 0 { print(t[0]); } // bce:keep
    if m > 2 { print(u[2]); } // bce:elide
}
