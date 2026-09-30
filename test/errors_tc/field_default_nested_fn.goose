// error: the default of field d of S leads to this construction of S, which takes it again
// A function declared in the default is part of it, as a block written there
// is: each check of the default declares it anew (§3.2, §7.5).
struct S {
    a: i64 = 1,
    d: i64 = block {
        fn h() -> i64 { S { .. }.a }
        break h();
    },
}
fn main() {
    let x = S { .. };
    print(x.d);
}
