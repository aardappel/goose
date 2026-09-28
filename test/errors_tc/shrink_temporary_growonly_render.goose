// error: cannot call format, which shrinks g: str runs it in the middle of rendering W { s: g[..], t: T { k: 1 } }
// error: which may refer into g
// Inside an expression as at a statement, str renders W's slice around the
// overload that clears g (§3.7, §5.1).
var g: i64[>..] = [1, 2];
struct T { k: i64 }
struct W { s: const i64[:], t: T }
fn format(out: u8[>..]&, t: T) { g.clear(); out.append("T"); }
fn count(s: u8[]) -> i64 { return s.len; }
fn main() {
    print(count(str(W { s: g[..], t: T { k: 1 } })));
}
