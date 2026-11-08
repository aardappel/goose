// error: s is or may point into storage partitioned by this par for
// A root struct's owner holds its components (§3.9): s.first[0] reads the
// component whose elements the iterations own (docs/design/child_threads.md
// §4.4).
root struct S { first: i64[>..], second: i64[>..] }
fn main() {
    var s = S { first: [1, 2, 3, 4, 5, 6, 7, 8], second: [0, 0] };
    par for &x in s.first { x = s.first[0] + 1; }
    print(s.first);
}
