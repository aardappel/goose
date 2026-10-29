// error: rooted at the array itself
root struct S { first: i64[>..], second: i64[>..] }
fn head(s: S&) -> i64& { s.first[0] }
fn main() { var s = S { [1], [2] }; print(s.second.index_of(head(s))); }
