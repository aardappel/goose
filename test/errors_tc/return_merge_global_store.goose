// error: storing a reference rooted at s, which does not outlive the destination
// A result merged from a global's view and a local's is rooted at the local
// (§9.2), so a callee handed it may not keep it in a global, even though
// the call happens to return the global's view here.
struct Keep { s: const u8[:] }
var kept: Keep[>..] = [];
var global: u8[>..] = "a long enough global text";

fn keep(s: const u8[:]) { kept.push(Keep { s: s }); }

fn longer(a: const u8[:], b: const u8[:]) -> const u8[:] {
    if a.len >= b.len { return a; }
    return b;
}

fn main() {
    var local: u8[>..] = "short";
    keep(longer(local, global));
    print(kept[0].s);
}
