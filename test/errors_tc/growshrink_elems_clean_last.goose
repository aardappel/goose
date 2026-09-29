// error: storing a reference into x, which holds a grow-shrink array
// growshrink_elems_clean_first with the calls swapped: whichever call
// checks k first, the one given an element of others may not store x
// (§5.2).
struct Pt { x: f64 }
struct Other { w: i64 }
struct Bag { n: i64, items: Pt[>..<] }
fn k(x: i64&, loc: (i64&)[>..]&) { loc.push(x); }
fn main() {
    var bag = Bag { n: 5, items: [Pt { x: 1.0 }] };
    var others: Other[>..<] = [Other { w: 7 }];
    var loc: (i64&)[>..] = [];
    k(others[0].w, loc);
    k(bag.n, loc);
    others.pop();
    others.push(Other { w: 99 });
    print(loc[0]);
}
