// error: storing a reference rooted at v, which does not outlive the destination
// A `.len` is no storage: print renders it from a temporary, which a format
// hook taking it by reference is given (§3.7), and which a reference the
// hook keeps outlives.
var arr: i64[>..] = [1, 2, 3];
var keep: (const i64&)? = null;
fn format(out: u8[>..]&, v: const i64&) { keep .= v; out.append("k"); }
fn main() {
    print(arr.len);
    if keep != null {
        let y: i64 = keep;
        print(y == 3);
    }
}
