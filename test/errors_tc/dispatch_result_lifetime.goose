// A dispatched result is rooted as deeply as the deepest case's result.
enum Choice { A { n: i64 }, B { n: i64 } }
fn view(c: Choice.A, s: const u8[:]) -> const u8[:] { return "abc"; }
fn view(c: Choice.B, s: const u8[:]) -> const u8[:] { return s; }
fn leak(c: Choice) -> const u8[:] {
    var local: u8[>..] = [];
    for i in 8 { local.push('a' + i as u8); }
    return view(c, local[..]);
}
fn main() {
    let c: Choice = Choice.B { n: 2 };
    print(leak(c));
}
