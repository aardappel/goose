// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes d rooted differently from the cycle's entry call
// A view read through a context the cycle passes on may be stored inside
// the cycle only where every activation's store lands in the same storage
// (§7.8). walk stores one into d's pointee, and its call back passes a
// global for d, which outlives run's text: the error is at the store.
struct Ctx { text: u8[>..]& }
struct Holder { view: const u8[:] }

var spare = Holder { view: "" };

recursive fn walk(c: Ctx&, d: Holder&, n: i64) {
    let begin = c.text.len;
    c.text.append("abc");
    d.view = c.text[begin..];
    if n > 0 { walk(c, spare, n - 1); }
}

fn run() {
    var text: u8[>..] = [];
    var c = Ctx { text: text };
    var h = Holder { view: "" };
    walk(c, h, 1);
}

fn main() {
    run();
    print(spare.view);
}
