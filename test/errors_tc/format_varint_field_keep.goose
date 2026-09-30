// error: storing a reference rooted at v, which does not outlive the destination
// An i64 read out of a varint field lies in no i64 storage (§3.6): a format
// hook taking it by reference is given a temporary holding the value the
// varint decodes to (§3.7), which a reference kept past the print outlives.
struct V { n: varint, w: i32 }
var gv = V { n: 5, w: 1 };
var keep: (const i64&)? = null;
fn format(out: u8[>..]&, v: const i64&) { keep .= v; out.append("k"); }
fn main() {
    print(gv.n);
    if keep != null {
        let y: i64 = keep;
        print(y == 5);
    }
}
