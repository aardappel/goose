// A recursive function returning views into an arena it reaches through a
// context (§7.8). The cycle is checked in rounds: a call back into it is
// given the roots its returns gave in the round before, the arena behind the
// context it passed on, so its result is rooted at that context. The context
// is threaded -- every call back into the cycle passes it on -- so that
// result may be stored into the context's table inside the cycle, besides
// being kept in a variable rebound to it.
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
