// error: cannot call drop, which shrinks a: the element pushed into a is still under construction, and the shrink would free the storage it is built in
// The same where a callee pops the array (§1.3).
struct N { r: i64&<u32>?, v: i64 }
fn drop(a: N[>..<]&) -> i64 { return a.pop().v; }
fn main() {
    var a: N[>..<] = [];
    a.push(N { r: null, v: 1 });
    a.push(N { r: null, v: drop(a) + 10 });
    print(a.len);
}
