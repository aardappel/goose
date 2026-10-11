// error: cannot use s.other in the value assigned to s.r
// Two reference fields of one type may refer to one array (§4.4).
fn twice(xs: const u8[:]) -> u8[>..<] {
    var r: u8[>..<] = [];
    r.append(xs);
    r.append(xs);
    return r;
}
struct D { n: i64, r: u8[>..<]&, other: u8[>..<]& }
fn run(s: D&) { s.r = twice(s.other); }
fn main() {
    var x: u8[>..<] = [1];
    var d = D { n: 0, r: x, other: x };
    run(d);
    print(x);
}
