// A format overload called in a loop body rebinds the global the loop's
// condition narrows, which the name scan cannot see: after the loop the
// global is not narrowed.
struct Show { x: i64 }
var n = 41;
var opt: i64? = null;
fn format(out: u8[>..]&, item: Show) { opt .= null; out.append("show"); }
fn main() {
    opt .= n;
    while opt { print(Show { x: 1 }); }
    print(opt + 1);
}
