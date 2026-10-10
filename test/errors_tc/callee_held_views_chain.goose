// error: call tricky stores a reference rooted at bx through its parameter d, whose argument may point into outer
// tricky moves views from t into s and from s into d: what d may get
// includes what t's storage holds, a reference to bx (§9.2).
struct B { v: i64 }
fn tricky(d: (B&)[:], s: (B&)[:], t: (B&)[:]) {
    d[0] .= s[0];
    s[0] .= t[0];
}
fn main() {
    var bo = B { v: 1 };
    var outer: (B&)[>..] = [];
    outer.push(bo);
    {
        var bx = B { v: 2 };
        var deep: (B&)[>..] = [];
        deep.push(bx);
        {
            var mid: (B&)[>..] = [];
            mid.push(bo);
            var mid2: (B&)[>..] = [];
            mid2.push(bo);
            let c = outer.len > 0;
            let s: (B&)[:] = if c { mid } else { mid2 };
            let d: (B&)[:] = if c { outer } else { mid };
            tricky(d, s, deep);
            tricky(d, s, deep);
        }
    }
    print(outer[0].v);
}
