// runtime-debug all-levels
var calls = 0;
fn value() -> i64 { calls++; return 7; }
fn main() {
    let zero = [0; 24];
    let ones = [-1; 24];
    let bytes: u8[31] = [0x55; 31];
    let mixed = [0x1234; 24];
    let effect = [value(); 24];
    let empty: i64[0] = [value(); 0];
    assert(calls == 2 && empty.len == 0);
    for i in 24 {
        assert(zero[i] == 0 && ones[i] == -1 && mixed[i] == 0x1234 && effect[i] == 7);
    }
    for b in bytes { assert(b == 0x55); }
    print("fixed array bulk fill: ok");
}
