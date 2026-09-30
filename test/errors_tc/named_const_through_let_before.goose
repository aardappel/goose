// error: the array size relies on N keeping its initializer's value, which rests on K's, but a writable reference bound to K at
// error: named_const_through_let_before.goose:9 may change it
// §11.1, §4.4: J's initializer makes K 13 before N's runs, so N would be 14
// while a, sized by N's initializer at 4, holds 4 elements.

fn bump(r: i64&) -> i64 { r += 10; r }

let K = 3;
let J = bump(K);
let N = K + 1;
var a: i64[N] = [1, 2, 3, 4];

fn main() {
    for i in 0..N { print(a[i]); }
}
