// error: K is bound to a writable reference here, through which it may change, while the array size at
// error: named_const_through_let.goose:10 relies on it keeping its initializer's value
// §11.1, §4.4: a's size is N's initializer's value, which rests on K's; J's
// initializer makes K 13 before N's runs, so N would be 14 while a holds 4
// elements.

fn bump(r: i64&) -> i64 { r += 10; r }

let K = 3;
var a: i64[N] = [1, 2, 3, 4];
let J = bump(K);
let N = K + 1;

fn main() {
    for i in 0..N { print(a[i]); }
}
