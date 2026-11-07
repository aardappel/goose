// error: may point into storage partitioned by this par for
// A byte view read out of a holder in the body may view the partitioned
// array's elements (docs/design/child_threads.md §4.4).
struct View { b: const u8[:] }
fn mix(dst: i64[:], v: View) {
    par for k in dst.len {
        let s = v.b;
        dst[k] = s[(k * 8 + 8) % s.len];
    }
}
fn main() {
    var a: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    mix(a, View { b: bytes_of(a) });
    print(a);
}
