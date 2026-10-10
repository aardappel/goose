// error: call tricky stores a reference rooted at bx through its parameter d, whose argument may point into outer
// The views tricky reads out of s include what it stores into s itself,
// here a reference to bx, which d's outer does not let in: the call
// judges the reads by what s's storage holds after all of the call's
// stores (§9.2).
struct B { v: i64 }
fn tricky(d: (B&)[:], s: (B&)[:], x: B&) {
    d[0] .= s[0];
    s[0] .= x;
}
fn main() {
    var bo = B { v: 1 };
    var outer: (B&)[>..] = [];
    outer.push(bo);
    {
        var bx = B { v: 2 };
        {
            var mid: (B&)[>..] = [];
            mid.push(bo);
            var mid2: (B&)[>..] = [];
            mid2.push(bo);
            let c = outer.len > 0;
            let s: (B&)[:] = if c { mid } else { mid2 };
            let d: (B&)[:] = if c { outer } else { mid };
            tricky(d, s, bx);
            tricky(d, s, bx);
        }
    }
    print(outer[0].v);
}
