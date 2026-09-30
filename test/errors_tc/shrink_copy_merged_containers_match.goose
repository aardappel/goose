// error: cannot clear buf while keep is still used: a reference into it was stored there
// es views a1's elements or a2's: the payload the match binder copies out
// of one holds what either's do, and a1's element views buf (§5.1).
enum E { Txt { s: const u8[:] }, Nothing { k: i64 } }
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var a1: E[>..] = [];
    a1.push(E.Txt { s: buf[..] });
    var a2: E[>..] = [];
    a2.push(E.Nothing { k: 1 });
    let c = buf.len > 5;
    let es = if c { a2[..] } else { a1[..] };
    var keep: E = E.Nothing { k: 0 };
    match es[0] {
        Txt t => { keep = t; },
        Nothing n => print(n.k),
    }
    buf.clear();
    buf.append("ZZZZZZZZ");
    match keep {
        Txt t => print(t.s),
        Nothing n => print(n.k),
    }
}
