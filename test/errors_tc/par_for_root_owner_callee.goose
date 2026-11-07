// error: s is or may point into storage partitioned by this par for
// The whole owner handed to a function the body calls reaches every
// component, the partitioned one too (docs/design/child_threads.md §4.4).
root struct S { first: i64[>..], second: i64[>..] }
fn head(s: S&) -> i64 { s.first[0] }
fn main() {
    var s = S { first: [1, 2, 3, 4, 5, 6, 7, 8], second: [0, 0] };
    par for &x in s.first { x = head(s) + 1; }
    print(s.first);
}
