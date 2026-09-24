// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes src rooted differently from the cycle's entry call
// A view of a slice parameter may be stored inside the cycle while every
// call back into it passes the parameter on (§7.8). This one passes a view
// of a local array of the activation instead, which the next activation
// would store into main's tokens: the error is at the store.
struct Tok { text: const u8[:] }

recursive fn scan(src: const u8[:], out: Tok[>..]&, n: i64) {
    out.push(Tok { text: src[0..2] });
    if n > 0 {
        var local: u8[4] = [120, 121, 122, 119];
        scan(local[..], out, n - 1);
    }
}

fn main() {
    var text: u8[>..] = "abcdef";
    var toks: Tok[>..] = [];
    scan(text[..], toks, 2);
    for &t in toks { print(t.text); }
}
