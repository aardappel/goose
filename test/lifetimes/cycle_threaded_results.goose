// A recursive function returning views into an arena it reaches through a
// context (§7.8). The cycle's scan cannot follow a view read out of the
// context's arena, so the base case's return, checked first, stands in for
// the prediction, and each call back into the cycle gives a result rooted at
// the context it passed on. The context is threaded -- every call back into
// the cycle passes it on -- so that result may be stored into the context's
// table inside the cycle, besides being kept in a variable rebound to it.
struct Ctx { arena: u8[>..]&, pieces: (const u8[:])[>..]& }

fn saved(c: Ctx&, s: const u8[:]) -> const u8[:] {
    let begin = c.arena.len;
    c.arena.append(s);
    return c.arena[begin..];
}

recursive fn fragment(c: Ctx&, n: i64) -> const u8[:] {
    if n == 0 { return saved(c, "leaf"); }
    var child = saved(c, "node");
    child = fragment(c, n - 1);
    c.pieces.push(child);
    return saved(c, if n % 2 == 0 { "even" } else { "odd" });
}

fn main() {
    var arena: u8[>..] = [];
    var pieces: (const u8[:])[>..] = [];
    var c = Ctx { arena: arena, pieces: pieces };
    let top = fragment(c, 4);
    print(top);
    for p in pieces { print(p); }
    print(arena.len);
}
