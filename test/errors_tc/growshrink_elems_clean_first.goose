// error: storing a reference into x, which holds a grow-shrink array
// k is checked first for bag.n, which bag's grow-shrink array of Pts cannot
// hold, so it may store x there; given an element of others, whose
// grow-shrink array holds i64s, it is checked again, and may not (§5.2):
// the pop frees the slot loc[1] points into and the push reuses it.
struct Pt { x: f64 }
struct Other { w: i64 }
struct Bag { n: i64, items: Pt[>..<] }
fn k(x: i64&, loc: (i64&)[>..]&) { loc.push(x); }
fn main() {
    var bag = Bag { n: 5, items: [Pt { x: 1.0 }] };
    var others: Other[>..<] = [Other { w: 7 }];
    var loc: (i64&)[>..] = [];
    k(bag.n, loc);
    k(others[0].w, loc);
    others.pop();
    others.push(Other { w: 99 });
    print(loc[1]);
}
