// An inlined body's u8[>..] call result too long for the limited array it is
// stored into fails its capacity check, as a direct call's does, rather than
// being dropped or written past the end (#332).
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

fn mk(a: const u8[:]) -> u8[>..] { return join(a, "yz"); }

fn main() {
    print(join("j/", "k"));   // join is called, mk inlined, as in #332.
    var d: u8[..4] = "";
    d = mk("w/");
    print("[", d, "]");
    d = mk("vw/x/");
    print("[", d, "]");
}
