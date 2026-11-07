// error: h is or may point into storage partitioned by this par for
// A holder of a reference to a slice variable, handed to a function that
// reads through it, leads where the slice points (docs/design/child_threads.md
// §4.4).
struct HS { s: i64[:]& }
fn first(h: HS) -> i64 { h.s[0] }
fn main() {
    var xs: i64[4] = [1, 1, 1, 1];
    let v = xs[0..4];
    let h = HS { s: v };
    par for k in 4 { xs[k] = first(h) + 1; }
    print(xs);
}
