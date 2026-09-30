// error: cannot call emit: it shrinks buf while t is still used, and t may refer into buf
// The payload t copies out of the element es views holds a view of buf
// (§5.1).
enum E { Txt { s: const u8[:] }, Nothing { k: i64 } }
fn emit(out: u8[>..]&, es: E[:]) {
    match es[0] {
        Txt t => {
            out.clear();
            out.append("ZZZZZZZZ");
            print(t.s);
        },
        Nothing n => print(n.k),
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var es: E[>..] = [];
    es.push(E.Txt { s: buf[..] });
    emit(buf, es[..]);
}
