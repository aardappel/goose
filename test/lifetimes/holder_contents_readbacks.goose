// What a holder of the activation's holds is what was stored there since it
// was made: a view or reference read out of it points where those stores
// did, exactly, so a shrink of storage none of them points into leaves it
// alone (§5.1), and the rules that need its identity take it as rooted
// there (§1.3, §3.3, §3.9, §4.4, §9.5).
import std;

struct LaneKeep { flow: u16[60], turn: u8, feed_turn: u8, feeder: i32 }
struct Item { pos: i32, id: u8 }
var items: Item[>..];
var lane_keep: LaneKeep[>..];
var saved: u8[>..];

fn rebuild() {
    items.clear();
    items.push(Item { pos: 1, id: 2 });
    lane_keep.clear();
    lane_keep.push(LaneKeep { flow: [0; 60], turn: 1, feed_turn: 0, feeder: 0 });
}

fn rebuild_and_use(name: u8[:]) {
    rebuild();
    print(name);
}

fn save_game(path: const u8[:]) -> bool {
    rebuild();
    saved.clear();
    saved.append(path);
    return true;
}

// A word of a parameter's bytes is one of the views the caller's storage
// holds, which each call judges.
fn exec_command(line: const u8[:]) {
    var t = trim(line);
    let hash = find(t, "#");
    if hash >= 0 { t = trim(t[..hash]); }
    guard t.len > 0;
    let w = words(t);
    if w[0] == "save" && !save_game(w[1]) { print("save failed"); }
}

struct N { v: i64, next: N&<u32>? }
struct H { r: N& }
struct G { r: i64[>..]& }
struct S { r: f64[>..<]& }

fn mk(y: i64[>..]&) -> i64[>..] {
    y.push(9);
    return [1, 2];
}
fn grow_into(x: i64[>..]&, y: i64[>..]&) { x.append(mk(y)); }
// A holder parameter whose every call passes references into one array
// holds references into that array exactly.
fn find(p: H, xs: N[>..]&) -> i64 { return xs.index_of(p.r); }
fn replace(x: f64[>..<]&, y: f64[>..<]&) { x = copy(y); }
fn walk(xs: f64[>..<]&, ys: f64[>..<]&) {
    for y in ys[..] {
        xs.pop();
        xs.push(0.5);
        print(y);
    }
}

fn main() {
    items.push(Item { pos: 0, id: 1 });
    var buf: u8[..64] = "save test.sav";
    let w = words(buf);
    rebuild_and_use(w[1]);

    var line: u8[>..] = [];
    line.append("save game.sav # and quit");
    exec_command(line);
    print(saved);
    var word = words(buf);
    save_game(word[1]);
    print(saved);

    // Rebuilt in a loop, it still holds only views of buf.
    var parts: (const u8[:])[>..] = [];
    for i in 3 {
        parts.push(buf[i..i + 2]);
        let p = parts[i];
        rebuild();
        print(p);
    }

    var ns: N[>..] = [N { v: 1 }, N { v: 2 }, N { v: 3 }];
    var spare: N[>..] = [N { v: 9 }];
    let h = H { r: ns[2] };
    print(ns.index_of(h.r));
    print(find(h, ns));
    print(find(H { r: ns[1] }, ns));
    ns[0].next .= h.r;
    let nx = ns[0].next;
    if nx { print(nx.v); }

    var a: i64[>..] = [1];
    var b: i64[>..] = [];
    let g = G { r: a };
    grow_into(b, g.r);
    print(a, b);

    var c: f64[>..<] = [1.0, 2.0, 3.0];
    var d: f64[>..<] = [4.0];
    let s = S { r: d };
    replace(s.r, c);
    walk(s.r, c);
    print(c, d);
}
