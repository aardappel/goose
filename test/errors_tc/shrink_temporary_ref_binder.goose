// error: cannot pop arr while keep is still used: a reference into it was stored there
// A slice read through a reference into a call's result points where the
// call put it, here into arr, whatever scope keeps it.
struct H { s: const i64[:] }
enum E { Has { h: H }, Not { n: i64 } }
fn wrap(a: i64[>..]&) -> E.. { E.Has { h: H { s: a[..1] } } }
fn main() {
    var arr: i64[>..] = [1, 2, 3];
    match wrap(arr) {
        Has &x => {
            var keep: const i64[:][>..] = [];
            keep.push(x.h.s);
            arr.pop(); arr.pop(); arr.pop();
            arr.push(42);
            print(keep[0][0]);
        },
        Not n => print(n.n),
    }
    print(arr.len);
}
