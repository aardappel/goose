// A reference read back out of a local holder points at one of the places
// the stores into the holder put there whose storage can hold its pointee:
// the i64 references a holder collects into a longer-lived pool are stored
// on into longer-lived storage, though the holder also views a shorter-lived
// u8 buffer (§9.5).
import std;

struct P { input: const u8[:], items: (i64&)[>..] }

fn fill(p: P&, pool: i64[>..]&) -> (i64&)[:] {
    p.items.push(pool.push(p.input.len));
    p.items.push(pool.push(p.input.len * 2));
    return p.items;
}

struct S { refs: (i64&)[>..] }

fn prep(s: S&, pool: i64[>..]&) {
    var text: u8[>..] = [];
    text.append("abc");
    var p = P { input: text, items: [] };
    let r = fill(p, pool);
    s.refs.append(r);
    let first: i64& = p.items[0];
    s.refs.push(first);
}

fn main() {
    var pool: i64[>..] = [];
    var s = S { refs: [] };
    prep(s, pool);
    print(s.refs.len, " ", s.refs[0], " ", s.refs[1], " ", s.refs[2]);
}
