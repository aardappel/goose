// error: would chain more than 16 instantiations of Seq, each needed by the one before (Seq<i64>, Seq<Pair<i64>>, Seq<Pair<Pair<i64>>>, ...)
// A variant's payload is checked with its enum, and wrapping the parameter
// in another generic type grows it as an array does (§3.2).
struct Pair<T> { a: T, b: T }
enum Seq<T> { Nil, Cons { v: T, rest: Seq<Pair<T>>? } }
fn main() {
    let s: Seq<i64> = Seq<i64>.Nil {};
    let n = match s { Cons c => c.v, Nil => 0 };
    print(n);
}
