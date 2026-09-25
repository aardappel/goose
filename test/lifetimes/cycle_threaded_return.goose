// f stores the result of its call back into the cycle, rooted at the context
// it reaches through outer's `c` and at `e`, read out of b's pointee: both
// are threaded parameter classes of outer whose ultimate roots are main's
// locals, outside every cycle, so the cycle may store what is rooted at
// them (§7.8).
struct Ctx { text: u8[>..]&, keep: (const u8[:])[>..]& }
struct Box { s: const u8[:] }

fn own(c: Ctx&, s: const u8[:]) -> const u8[:] {
    let begin = c.text.len;
    c.text.append(s);
    return c.text[begin..];
}

fn outer(c: Ctx&, b: Box&) -> i64 {
    let e = b.s;
    recursive fn f(k: i64) -> const u8[:] {
        if k == 0 { return own(c, "x"); }
        let r = f(k - 1);
        c.keep.push(r);
        return e;
    }
    return f(2).len;
}

fn main() {
    var text: u8[>..] = [];
    var keep: (const u8[:])[>..] = [];
    var more: u8[>..] = "abc";
    var c = Ctx { text: text, keep: keep };
    var bx = Box { s: more[..] };
    print(outer(c, bx));
    for s in keep { print(s); }
}
