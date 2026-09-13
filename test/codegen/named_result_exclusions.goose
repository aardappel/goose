// A named result is not built at a destination another value also claims:
// a long-distance return into the function, or a second result position.
fn each<F>(n: i64) { for i in n { F(i); } }
fn build(n: i64) -> u8[] {
    var s: u8[>..] = [];
    s.push('a');
    each(n) { i =>
        s.push('b');
        if i == 1 { return copy(s); }
    }
    s.push('z');
    return s;
}
fn leave() { return [7, 8, 9] from grow; }
fn grow(n: i64) -> i64[>..] {
    var out: i64[>..] = [1, 2];
    out.push(3);
    if n > 0 { leave(); }
    return out;
}
fn two() -> u8[>..], u8[>..] {
    var s: u8[>..] = [];
    s.push('a');
    s.push('b');
    return s, s;
}
fn two_arr() -> u8[], u8[] {
    var s: u8[>..] = [];
    s.push('c');
    s.push('d');
    return s, s;
}
fn main() {
    print(build(5), " ", build(0));
    var g = grow(1);
    g.push(4);
    print(g, " ", grow(0));
    let a, b = two();
    print(a, " ", b);
    let c, d = two_arr();
    print(c, " ", d);
}
