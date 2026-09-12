// Case functions are alternatives of one call: its result is writable only
// if every case returns a writable reference.
enum Choice { A { n: i64 }, B { n: i64 } }
fn pick(c: Choice.A, s: u8[:]) -> u8[:] { return s; }
fn pick(c: Choice.B, s: u8[:]) -> u8[:] { return "abc"; }
fn main() {
    var buf: u8[3] = "xyz";
    let c: Choice = Choice.B { n: 2 };
    let v = pick(c, buf[..]);
    v[0] = 'Q';
    print(v);
}
