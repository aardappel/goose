// A by-value for binding is a copy of the element: a write would update
// the copy and nothing else, so it is an error naming the &x form (§6.5).
struct Pt { x: i64, y: i64 }
fn main() {
    var pts: Pt[>..] = [Pt { x: 1, y: 2 }];
    for p in pts { p.x += 1; }
    print(pts);
}
