// error: array size K names a local variable, not a constant
// A type argument is written at the call, where main's K hides the global
// (§3.3, §7.7).
let K = 3;

fn size<T>() -> i64 {
    let one: T[1] = [default<T>()];
    bytes_of(one).len
}

fn main() {
    let K = 5;
    print(size<i64[K]>(), " ", K);
}
