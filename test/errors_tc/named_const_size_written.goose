// error: K is bound to a writable reference here, through which it may change, while the array size at
// error: named_const_size_written.goose:9 relies on it keeping its initializer's value, as a named constant
// §11.1, §4.4: a's type takes K at its initializer's value, 3, while bump
// makes K 13, after which the loop would index past a's end.

fn bump(r: i64&) { r += 10; }

let K: i64 = 3;
var a: i64[K] = [1, 2, 3];

fn main() {
    bump(K);
    for i in 0..K { print(a[i]); }
}
