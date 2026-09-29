// Which elements a parameter's grow-shrink array can hold is part of its
// class (§5.2): a reference whose root holds only arrays that cannot hold
// its pointee may be stored, at every call that gives one, in either
// order, whichever array it is, passed on through another function, or
// held by a value. Shrinking those arrays frees nothing loc points into.
struct Pt { x: f64 }
struct Name { first: u8, last: u8 }
struct Bag { n: i64, items: Pt[>..<] }
struct Tags { count: i64, names: Name[>..<] }
struct H { r: i64& }

fn k(x: i64&, loc: (i64&)[>..]&) { loc.push(x); }
fn mid(x: i64&, loc: (i64&)[>..]&) { k(x, loc); }
fn held(h: H, loc: (i64&)[>..]&) { loc.push(h.r); }

fn main() {
    var bag = Bag { n: 5, items: [Pt { x: 1.0 }] };
    var tags = Tags { count: 2, names: [Name { first: 65, last: 66 }] };
    var loc: (i64&)[>..] = [];
    k(bag.n, loc);
    k(tags.count, loc);
    mid(tags.count, loc);
    mid(bag.n, loc);
    held(H { r: bag.n }, loc);
    held(H { r: tags.count }, loc);
    bag.items.pop();
    tags.names.clear();
    bag.n = 6;
    tags.count = 3;
    for r in loc { print(r); }
    print(bag.items.len, tags.names.len);
}
