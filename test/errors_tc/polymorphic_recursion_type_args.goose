// error: instantiating grow<T = u8[2][2][2][2][2][2][2][2][2][2][2][2][2][2][2][2]>(i64) would put more than 16 specializations of grow in progress on this call path
// error: a recursive call must reach a finite set of instantiations
// The same through an explicit type argument no parameter mentions: each
// round of grow<T> instantiates grow<T[2]> (§7.7, §7.8).
fn size<T>() -> i64 {
    let one: T[1] = [default<T>()];
    bytes_of(one).len
}
recursive fn grow<T>(n: i64) -> i64 {
    if n == 0 { return size<T>(); }
    grow<T[2]>(n - 1)
}
fn main() { print(grow<u8>(3)); }
