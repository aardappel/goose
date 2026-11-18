// A u8[>..] result an inlined body returns from a call of its own, stored
// into a limited array, is copied in as a direct call's result is: at -O1
// and -O2 it used to leave the destination empty (#332).
import std;

fn norm(p: const u8[:]) -> u8[>..] {
    var parts: const u8[:][>..] = [];
    each_split(p, '/') { parts.push(it); };
    var out: u8[>..] = [];
    for part in parts { out.append(part); }
    return out;
}

fn join(a: const u8[:], b: const u8[:]) -> u8[>..] {
    var s: u8[>..] = [];
    s.append(a);
    s.append(b);
    return norm(s);
}

// Inlined to the bare call `join(...)`.
fn mk(a: const u8[:]) -> u8[>..] { return join(a, "y"); }

// Inlined as a block that returns the call.
var calls = 0;
fn counted() -> u8[>..] {
    calls++;
    return join("c/", "d");
}

// Two returns of a call.
fn pick(k: bool) -> u8[>..] {
    if k { return join("p/", "q"); }
    return join("r/", "s");
}

struct Holder { name: u8[..16], n: i64 }

fn main() {
    var d: u8[..64] = "";
    d = mk("x/");
    print("[", d, "]");
    d = mk("a/b/");
    print("[", d, "]");
    var e: u8[..64] = mk("e/");
    print("[", e, "]");
    d = counted();
    print("[", d, "] ", calls);
    d = pick(calls > 0);
    print("[", d, "]");
    d = pick(calls > 1);
    print("[", d, "]");
    var h = Holder { name: "", n: 1 };
    h.name = mk("h/");
    print("[", h.name, "] ", h.n);
    var hs: u8[..8][3] = ["", "", ""];
    hs[1] = mk("i/");
    print("[", hs[0], "][", hs[1], "][", hs[2], "]");
}
