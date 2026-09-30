// error: cannot write through this reference
// A format hook taking an i64 by reference is given a varint field's value
// in a read-only temporary (§3.6, §3.7): a varint is written only at
// construction, and no i64 storage holds the value.
struct V { n: varint, w: i32 }
var gv = V { n: 5, w: 1 };
fn format(out: u8[>..]&, v: i64&) { v = 300; out.append("w"); }
fn main() {
    print(gv.n);
}
