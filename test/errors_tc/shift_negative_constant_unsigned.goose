// error: constant -1 does not fit u64
// §6.1: -1 << k meets a u64 operand as -1 would.

fn main() {
    var k = 2;
    k += 1;
    let m: u64 = 8;
    print(m | (-1 << k));
}
