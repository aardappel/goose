// Views of static data stay static data through call results and through
// the arrays that hold only them, so a shrink of a global array whose
// elements could hold the bytes leaves them alone (§5.1).
import std;

struct Item { pos: i32, id: u8 }
var items: Item[>..];
var label: const u8[:] = "";
var labels: (const u8[:])[>..];

fn reset() {
    items.clear();
    items.push(Item { pos: 1, id: 2 });
}

fn rebuild_and_use(name: u8[:]) {
    reset();
    print(name);
}

fn why(x: i64) -> const u8[:] {
    if x > 0 { return "pos"; }
    return "neg";
}

fn none() -> const u8[:] { "" }

fn names() -> const u8[:][>..] {
    var out: const u8[:][>..] = [];
    out.push("ab");
    out.push(why(-1));
    return out;
}

fn main() {
    items.push(Item { pos: 0, id: 1 });
    let w = words("save test.sav");
    rebuild_and_use(w[1]);
    let m = why(3);
    reset();
    print(m);
    let parts = split("a,b", ',');
    let p = parts[1];
    reset();
    print(p);
    var h: const u8[:][>..] = ["x"];
    h.push(why(0));
    let n = h[1];
    reset();
    print(n);
    let ns = names();
    let q = ns[1];
    reset();
    print(q);
    let e = none();
    reset();
    print(e.len);
    label = why(1);
    labels.push(why(2));
    reset();
    print(label);
    print(labels[0]);
    print(items.len);
}
