// error: array size K names a local variable, not a constant
// A literal's type is written where the literal is (§3.3): here the `..`
// would have filled a with 3 elements, the global K's.
let K = 3;

struct P<T> { a: T }

fn main() {
    let K = 5;
    let p = P<i64[K]> { .. };
    print(p.a.len, " ", K);
}
