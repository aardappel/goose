// error: K is bound to a writable reference here, through which it may change, while the use as a literal at
// error: relies on it keeping its initializer's value, as a named constant
// §3.1, §6.4: the loop's breaks join K with a u8, so K adapts to u8 as a
// literal, which relies on K keeping its initializer's value; the next
// iteration would break with 1003.

fn bump(r: i64&) { r += 1000; }
let K = 3;

fn main() {
    let b: u8 = 7;
    for i in 2 {
        let x = loop { if i >= 0 { break K; } break b; };
        print(x, " ", K, "\n");
        bump(K);
    }
}
