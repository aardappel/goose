// error: the use as a literal relies on K keeping its initializer's value, but a writable reference bound to K at
// §3.1, §6.4: a block's break joins K with the u8 its tail gives, adapting
// K as a literal, after bump may already have changed it.

fn bump(r: i64&) { r += 1000; }
let K = 3;

fn main() {
    let b: u8 = 7;
    bump(K);
    let x = block { if b > 3 { break K; } b };
    print(x, " ", K);
}
