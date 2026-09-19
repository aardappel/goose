// error: cannot call twice in the value assigned to x: twice uses x
// The block runs inside twice, reading x while twice builds its new
// contents (§7.6).
fn twice<F>(n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    for i in 0..n { out.push(F(i)); }
    for i in 0..n { out.push(F(i) + 1); }
    return out;
}
fn main() {
    var x: u8[>..] = [];
    x.append("abcd");
    x = twice(4) { x[it] };
    print(x);
}
