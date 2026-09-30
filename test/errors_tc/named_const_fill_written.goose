// error: K is bound to a writable reference here, through which it may change, while the array fill count at
// error: named_const_fill_written.goose:10 relies on it keeping its initializer's value
// §11.1, §4.4: the fill literal takes K * 2 at K's initializer's value,
// 6 elements, which bump then makes K * 2 disagree with.

fn bump(r: i64&) { r += 10; }
let K = 3;

fn main() {
    let a = [0; K * 2];
    bump(K);
    print(a.len, " ", K * 2);
}
