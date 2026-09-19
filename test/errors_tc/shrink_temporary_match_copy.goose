// error: cannot pop arr while x is still used: a reference into it was stored there
// A payload copied out of a call's result holds what the call put in it.
struct H { r: i64& }
enum E { Has { h: H }, Not { n: i64 } }
fn wrap(a: i64[>..]&) -> E { E.Has { h: H { r: a[0] } } }
fn main() {
    var arr: i64[>..] = [1, 2, 3];
    match wrap(arr) {
        Has x => { arr.pop(); arr.pop(); arr.pop(); arr.push(42); print(x.h.r); },
        Not n => print(n.n),
    }
    print(arr.len);
}
