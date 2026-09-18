// x.copy() is copy(x) (§7.1): a fresh copy of the stored value, or of a
// reference's pointee (§4.1), wherever it stands -- at a value destination,
// as an argument of a builtin or a function, nested in another call's
// argument, and as a dispatched call's other argument, receiver included.
// Copies that are changed leave their originals as they were, so the output
// shows that they are copies.
struct Txt { body: u8[] }
struct Two { t: Txt, n: i64 }
struct Bag { items: i64[>..<] }
enum Shape { Circle { r: i64 }, Point { k: i64 } }

fn len1(t: Txt) -> i64 { t.body.len }
fn slen(s: u8[:]) -> i64 { s.len }
fn wrap(t: Txt) -> Txt { t.copy() }
fn viaref(r: Txt&) -> i64 { len1(r.copy()) }
fn two(p: Two) -> i64 { p.t.body.len + p.n }
fn twice(n: i64) -> i64 { n * 2 }
fn bump(var n: i64) -> i64 { n += 1; n }
// An untyped parameter takes a copy by value and storage by reference.
fn grow(var b) -> i64 { b.items.push(9); b.items.len }
fn size(s: Shape.Circle, t: Txt) -> i64 { s.r + t.body.len }
fn size(s: Shape.Point, t: Txt) -> i64 { s.k * t.body.len }
fn pick(t: Txt, s: Shape.Circle) -> Txt { t.copy() }
fn pick(t: Txt, s: Shape.Point) -> Txt { Txt { body: "p" } }
fn fill(var bag: Bag, s: Shape.Circle) -> i64 { bag.items.push(s.r); bag.items.len }
fn fill(var bag: Bag, s: Shape.Point) -> i64 { bag.items.clear(); bag.items.len }

fn main() {
    let n = 5;
    print(n.copy());
    let t = Txt { body: "hey" };
    let u: Txt = t.copy();
    print(u.body);

    // Value destinations.
    var bag = Bag { items: [1, 2] };
    var b2: Bag = bag.copy();
    b2.items.push(3);
    var b3 = Bag { items: [] };
    b3 = bag.copy();
    b3.items.clear();
    var xs = bag.items.copy();
    xs.push(4);
    let pair = Two { t: t.copy(), n: 1 };
    var ts: Txt[>..] = [];
    ts.push(t.copy());
    ts.push(wrap(t.copy()));
    print(bag.items, " ", b2.items, " ", b3.items, " ", xs, " ", pair.t.body, " ", ts.len);

    // Arguments of builtins (copy(x) too) and of functions.
    print(str(t.copy(), bag.copy()), " ", str(copy(t)), " ", copy(n));
    var out: u8[>..] = [];
    out.format(n.copy(), t.copy());
    print(out, " ", len1(t.copy()), " ", t.copy().len1(), " ", viaref(t));
    print(grow(bag.copy()), " ", bag.items.len, " ", bump(n.copy()), " ", n);

    // Nested in another call's argument.
    print(twice(bump(n.copy())), " ", n.copy().bump().twice());
    print(str(len1(t.copy()), t.copy().len1()), " ", slen(str(t.copy())));
    print(len1(wrap(t.copy())), " ", t.copy().wrap().len1(), " ", wrap(wrap(t.copy())).len1(),
          " ", two(Two { t: t.copy(), n: 1 }));
    print(twice(grow(bag.copy())), " ", bag.copy().grow().twice(), " ", bag.items.len);

    // A dispatched call's other argument, receiver included.
    let c: Shape = Shape.Circle { r: 2 };
    let p: Shape = Shape.Point { k: 4 };
    print(size(c, t.copy()), " ", size(p, t.copy()), " ", c.size(t.copy()),
          " ", size(p, wrap(t.copy())));
    print(t.copy().pick(c).body, " ", pick(t.copy(), p).body, " ", len1(pick(t.copy(), c)));
    print(bag.copy().fill(c), " ", fill(bag.copy(), p), " ", bag.items.len);
}
