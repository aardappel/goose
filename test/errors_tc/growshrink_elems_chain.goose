// error: storing a reference into x, which holds a grow-shrink array
// r may point into bag or into an element of others, so mid's class for x
// may point into a grow-shrink array that is not its root's own; passed
// on, k's class names that same array, whatever bag's can hold, and k may
// not store x (§5.2).
struct Pt { x: f64 }
struct Other { w: i64 }
struct Bag { n: i64, items: Pt[>..<] }
fn k(x: i64&, loc: (i64&)[>..]&) { loc.push(x); }
fn mid(x: i64&, loc: (i64&)[>..]&) { k(x, loc); }
fn main() {
    var bag = Bag { n: 5, items: [Pt { x: 1.0 }] };
    var others: Other[>..<] = [Other { w: 7 }];
    var loc: (i64&)[>..] = [];
    let c = bag.n < 3;
    let r: i64& = if c { bag.n } else { others[0].w };
    mid(r, loc);
    others.pop();
    others.push(Other { w: 99 });
    print(loc[0]);
}
