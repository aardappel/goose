// A recursive call may rebind what a format overload names, which printing
// reaches without naming it: the read after the back edge cannot rely on g.
struct Show { x: i64 }
var n = 41;
var g: i64? = null;
fn format(out: u8[>..]&, s: Show) { g .= null; out.append("s"); }
recursive fn r(k: i64) {
    if k > 0 { r(k - 1); }
    print(g + 1);
    print(Show { x: k });
}
fn main() {
    g .= n;
    r(1);
}
