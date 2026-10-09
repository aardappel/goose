// An inlined body's value is its final return's only where no other return
// leaves it: position's `return i` from inside its loop gives the call
// another value than the `-1` after the loop, so nothing bounds the index
// past it by the -1, and the check stays (and fires on "12e").

fn pos(xs: u8[:], v: u8) -> i64 {
    for x, i in xs { if x == v { return i; } }
    return -1;
}

fn after(s: u8[:]) -> i64 {
    let e = pos(s, 'e');
    if e < 0 { return 0; }
    return s[e + 1]; // bce:keep
}

fn main() {
    print(after("1e-5"));
    print(after("125"));
    print(after("12e"));
}
