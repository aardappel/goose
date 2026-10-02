// error: K is bound to a writable reference here, through which it may change, while the use as a literal at
// error: named_const_literal_written.goose:11 relies on it keeping its initializer's value, as a named constant
// §3.1, §4.4: the store adapts K to u8 as a literal, which relies on K
// keeping its initializer's value; bump could change it to one that does
// not fit.

fn bump(r: i64&) { r += 1000; }
let K = 3;

fn main() {
    let small: u8 = K;
    bump(K);
    print(small, " ", K);
}
