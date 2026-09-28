// error: cannot pop a: the element pushed into a is still under construction, and the shrink would free the storage it is built in
// An element holding a relative reference is built in its slot, at the
// array's top (§4.3), which the pop moves down under it (§1.3).
struct N { r: i64&<u32>?, v: i64 }
fn main() {
    var a: N[>..<] = [];
    a.push(N { r: null, v: 1 });
    a.push(N { r: null, v: 2 });
    a.push(N { r: null, v: a.pop().v + 10 });
    print(a.len);
}
