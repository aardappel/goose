// error: call copy_into stores a reference rooted at bi through its parameter dst, whose argument may point into outer
// copy_into stores views read out of src into dst, which may each be outer
// or inner: what inner holds, a reference to bi, may land in outer, which
// bi does not outlive (§9.2).
import std;
struct B { v: i64 }
fn main() {
    var bo = B { v: 1 };
    var outer: (B&)[>..] = [];
    outer.push(bo);
    {
        var bi = B { v: 2 };
        var inner: (B&)[>..] = [];
        inner.push(bi);
        let c = outer.len > 0;
        let s: (B&)[:] = if c { inner } else { outer };
        let d: (B&)[:] = if c { outer } else { inner };
        copy_into(d, s);
    }
    print(outer[0].v);
}
