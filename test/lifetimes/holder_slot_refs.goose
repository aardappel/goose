// A holder's reference to a slot that holds references leads to what the slot
// holds (§5.1): a holder there holds what its own stores put there, a slice
// slot views only what outlives it, and a global slot only globals, which
// the judgement of the globals follows. A shrink of anything else leaves the
// holder in use.
struct B { s: u8[:] }
struct R { r: B& }
struct Q { q: u8[:]& }
var GA: u8[>..] = [];
var GB: u8[4] = [1, 2, 3, 4];
var GS: u8[:] = GB[..];
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    // bx's stores say its slice views b.
    var bx = B { s: b[..] };
    var rr = R { r: bx };
    a.clear();
    a.append("def");
    print(rr.r.s, " ", a);
    // s outlives c, so it cannot view it.
    var s = b[0..2];
    {
        var c: u8[>..] = [];
        c.append("pq");
        var qq = Q { q: s };
        c.clear();
        c.append("rs");
        print(qq.q, " ", c);
    }
    // GS views GB, as every binding of it on record says.
    GA.append("gh");
    var gq = Q { q: GS };
    GA.clear();
    GA.append("ij");
    print(gq.q.len, " ", GA);
}
