// error: storing a reference rooted at v, which does not outlive the destination
// An f32 computed by an operator is no storage: print renders it from a
// temporary, which a format hook taking it by reference is given (§3.7),
// and which a reference the hook keeps outlives.
var gf: f32 = 1.5;
var keep: (const f32&)? = null;
fn format(out: u8[>..]&, v: const f32&) { keep .= v; out.append("k"); }
fn main() {
    print(gf * 2.0);
    if keep != null {
        let y: f32 = keep;
        print(y == 3.0);
    }
}
