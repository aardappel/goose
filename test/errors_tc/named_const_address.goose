// error: K is bound to a writable reference here, through which it may change, while the array size at
// error: named_const_address.goose:7 relies on it keeping its initializer's value
// §11.1, §3.8, §4.4: &K of the `let` global K is a writable reference to
// it, however it is then used.

let K = 3;
var a: i64[K] = [1, 2, 3];

fn main() {
    let r = &K;
    r = 4;
    print(a[K - 1]);
}
