// A slice parameter is threaded when every call back into the cycle passes
// it a subslice of itself (§7.8): `src[n..]` is rooted exactly where `src`
// is, so every activation's `src` views the array the entry call passed,
// and views of it may be stored into a pool inside the cycle.
struct Tok { text: const u8[:], rest: i64 }

recursive fn tokens(src: const u8[:], out: Tok[>..]&) -> i64 {
    if src.len == 0 { return 0; }
    var n = 1;
    while n < src.len && src[n] != 32 && src[0] != 32 { n++; }
    out.push(Tok { text: src[..n], rest: src.len - n });
    return 1 + tokens(src[n..], out);
}

fn main() {
    var text: u8[>..] = "let x be 42";
    var toks: Tok[>..] = [];
    print(tokens(text[..], toks));
    for &t in toks { print("[", t.text, "] ", t.rest); }
}
