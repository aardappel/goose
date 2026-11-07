// error: cannot call shrink, which shrinks a: the elements of a, which the statement reads after it, may be in a
// The receiver's elements and length are read before its arguments run, as
// a slice's object's are before its bounds: a count that shrinks the array
// would leave the chunk past its end (§5.1).
fn shrink(a: i64[>..]&) -> i64 { a.pop(); return 2; }
fn main() {
    var a: i64[>..] = [1, 2, 3, 4];
    print(a.chunk(1, shrink(a)));
}
