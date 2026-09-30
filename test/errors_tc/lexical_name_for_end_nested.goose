// error: cannot negate a value of type fn
// So it is where a nested function hides the global K, whose -K a u8 could
// not compute (§6.5, §11.1).
let K: u8 = 200;

fn main() {
    fn K(x: i64) -> i64 { x }
    var s = 0;
    for i: i8 in -9..-K { s += i; }
    print(s, " ", K(1));
}
