// error: cannot call format, which shrinks g: g[0], evaluated earlier in the statement
// error: may still refer into g
// str renders T through its format overload, which clears g while the
// enclosing call holds a reference into it (§3.7, §5.1).
var g: i64[>..] = [1, 2];
struct T { k: i64 }
fn format(out: u8[>..]&, t: T) { g.clear(); out.append("T"); }
fn use(r: i64&, s: u8[]) -> i64 { return r + s.len; }
fn main() {
    print(use(g[0], str(T { k: 1 })));
}
