// A recursive call may instantiate its callee with other types than its own
// (§7.8), as long as the instantiations it reaches are finite: a cycle whose
// types repeat ends in a back edge, and a descent through a nested type ends
// at its innermost level.

// f<A, B> calls f<B, A>, which calls f<A, B> again: the back edge reuses it.
recursive fn flip<A, B>(a: A, b: B, n: i64) -> i64 {
    if n == 0 { return 0; }
    flip(b, a, n - 1) + 1
}

// A three-step cycle through explicit type arguments no parameter mentions.
fn size<T>() -> i64 {
    let one: T[1] = [default<T>()];
    bytes_of(one).len
}
recursive fn rotate<A, B, C>(n: i64) -> i64 {
    if n == 0 { return 0; }
    size<A>() * 10 + rotate<B, C, A>(n - 1)
}

// One specialization of depth<T> per level: the sixteen in progress at the
// innermost call are the most one function may have on a call path.
type Deep = i64[1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1];
fn depth(x: i64) -> i64 { x }
fn depth<T>(x: T[1]) -> i64 { depth(x[0]) + 1 }

fn main() {
    let x: i32 = 1;
    let y = 2.5;
    print(flip(x, y, 5));
    print(rotate<u8, i16, i64>(4));
    let deep: Deep = [[[[[[[[[[[[[[[[100]]]]]]]]]]]]]]]];
    print(depth(deep));
}
