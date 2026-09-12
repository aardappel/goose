// Case results merge at every position of a multi-value call: the slice is
// writable only if every case returns a writable one.
enum Choice { A { n: i64 }, B { n: i64 } }
fn pick(c: Choice.A, s: u8[:]) -> i64, u8[:] { return 1, s; }
fn pick(c: Choice.B, s: u8[:]) -> i64, u8[:] { return 2, "abc"; }
fn main() {
    var buf: u8[3] = "xyz";
    let c: Choice = Choice.B { n: 2 };
    let k, v = pick(c, buf[..]);
    v[0] = 'Q';
    print(k, v);
}
