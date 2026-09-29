// Structs the program reaches only through references: it never builds or
// holds one by value, so a read, a write or a print through a reference is
// all that needs the C struct's body. Each case has types of its own, so
// no other case emits them first: an optional reference parameter, a plain
// reference to an element of a global array that never grows, a nested
// struct's field, a struct linking to its own type, stores, a printed
// struct holding a reference to another, an ADT and one of its variants,
// and the reference a call returns.
struct Cell { v: i64 }
fn show(r: Cell&?) -> i64 { if r { return r.v; } return -1; }

struct Slot { id: i64, weight: i64 }
var slots: Slot[>..] = [];
fn weigh(s: Slot&) -> i64 { return s.id + s.weight * 2; }
fn total() -> i64 {
    var n = 0;
    for &s in slots { n += weigh(s); }
    return n;
}

struct Inner { x: i64 }
struct Outer { a: i64, inner: Inner }
fn inner_x(o: Outer&?) -> i64 { if o { return o.inner.x + o.a; } return -1; }

struct Link { v: i64, next: Link&? }
fn chain(l: Link&?) -> i64 {
    var n = 0;
    var c = l;
    while c { n += c.v; c .= c.next; }
    return n;
}

struct Counter { hits: i64 }
fn hit(c: Counter&?) -> bool {
    if c { c.hits += 1; c.hits = c.hits * 2; return true; }
    return false;
}

struct Point { x: i32, y: i32 }
struct Named { id: i64, at: Point&? }
fn describe(n: Named&?) -> u8[] { if n { return str(n); } return str("no name"); }

enum Shape { Circle { r: f64 }, Square { s: f64 } }
fn shape(s: Shape&?) { if s { print(s); } else { print("no shape"); } }
fn radius(c: Shape.Circle&?) -> f64 { if c { return c.r; } return -1.0; }

struct Pair { a: i64, b: i64 }
fn same(p: Pair&) -> Pair& { return p; }
fn second(p: Pair&?) -> i64 { if p { return same(p).b; } return -1; }

fn main() {
    print(show(null), " ", show(default<Cell?>()));
    print(total(), " ", slots.len);
    print(inner_x(null));
    print(chain(null));
    print(hit(null));
    print(describe(null));
    shape(null);
    print(radius(null));
    print(second(null));
}
