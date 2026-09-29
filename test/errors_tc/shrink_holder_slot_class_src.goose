// error: cannot clear GA while hl is still used: a reference to a slice that may point into it was stored there
// The reference read out of the caller's element refers to a slot of the
// caller's, which may hold a view of GA: here s, which does (§5.1).
struct R { r: u8[:]& }
var GA: u8[>..] = [];
fn use(hs: R[:]) {
    var hl: R[>..] = [];
    hl.push(R { r: hs[0].r });
    GA.clear();
    GA.append("ZZZZZZZZ");
    print(hl[0].r);
}
fn main() {
    GA.append("abc");
    var s = GA[..];
    var rs: R[>..] = [];
    rs.push(R { r: s });
    use(rs[..]);
}
