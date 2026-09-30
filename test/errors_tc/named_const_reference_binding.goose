// error: K is bound to a writable reference here, through which it may change, while the array fill count at
// error: named_const_reference_binding.goose:7 relies on it keeping its initializer's value
// §11.1, §3.8, §4.4: `let r .= K` binds r to K itself, and assigning r
// writes K.

let K = 3;
let ones = [1; K];
fn main() {
    let r .= K;
    r = 4;
    print(ones.len, " ", K);
}
