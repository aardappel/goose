// error: storing a reference rooted at v, which does not outlive the destination
// A call's reference to a varint field decays to the i64 the varint decodes
// to, which no i64 storage holds (§3.6): a format hook taking it by
// reference is given a temporary holding that value (§3.7), which a
// reference kept past the print outlives.
struct V { n: varint, w: i32 }
var gv = V { n: 5, w: 1 };
var keep: (const i64&)? = null;
fn format(out: u8[>..]&, v: const i64&) { keep .= v; out.append("k"); }
fn get(v: V&) -> const varint& { v.n }
fn main() {
    print(get(gv));
    if keep != null {
        let y: i64 = keep;
        print(y == 5);
    }
}
