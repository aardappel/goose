// A string literal taken as a runtime-capacity limited array (`u8[..]`)
// has that array's layout (C.2), capacity then length then slots, whose
// elements an assignment copies into a destination of its own capacity
// (§4.2): a literal, the empty one, one filling the capacity, a branch's,
// into a variable and into a field; and one past the capacity aborts.
struct Named { tag: u8, name: u8[..] }

fn main() {
    var lim: u8[..] = [..8];
    lim = "abc";
    print(lim, " ", lim.len, " ", lim.cap);
    lim = "";
    print("[", lim, "] ", lim.len, " ", lim.cap);
    lim = "abcdefgh";
    print(lim, " ", lim.len, " ", lim.cap);
    let short = lim.len < 4;
    lim = if short { "short" } else { "long" };
    print(lim, " ", lim.len, " ", lim.cap);
    lim = match lim.len { 4 => "four", _ => "other" };
    print(lim, " ", lim.len, " ", lim.cap);
    var n = Named { tag: 1, name: [..4] };
    n.name = "xy";
    print(n.tag, " ", n.name, " ", n.name.len, " ", n.name.cap);
    lim = "abcdefghi";
    print("not reached");
}
