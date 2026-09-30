// error: print cannot render K.., which reaches itself through references, around the format overload for M
// An ADT linking to itself (§3.4) is rendered level by level, which runs
// no format overload for a part of it (§3.7), here behind a self-relative
// link.
struct M { v: i64 }
fn format(out: u8[>..]&, m: M) { format(out, "M", m.v); }
enum K { Nil, Cons { v: i64, next: K..&<u16>? }, Em { m: M } }
fn main() {
    var ks: K..[>..] = [];
    let k0 .= ks.push(K.Em { m: M { v: 4 } });
    let k1 .= ks.push(K.Cons { v: 1, next: k0 });
    print(k1);
}
