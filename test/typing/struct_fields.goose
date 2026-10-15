// fields(v) F and fields(&v) F (§12): F("name", v.name), or
// F("name", &v.name), for each field of v's struct in declaration order.
// A block is checked at each field's type and a named overload set resolved
// per field; overloads walk nested structs and arrays, reading and writing;
// a block's return leaves the function it is written in; pads are skipped,
// and a struct without fields calls nothing. The value may be a variable,
// a reference, a global, a field path or an element at an index no call
// can assign (a let, a parameter, a for index), and a member call names the
// builtin too. Each call reads the value as it is then.
struct Told { notice: i64, a: i64 }
struct W { turn: i64, asked: bool, hp: i64[4], told: Told }
struct Empty {}
struct Padded { a: i64, pad 4, b: i32 }
struct Pair<T> { first: T, second: T }
struct Named { name: u8[], n: i64 }
struct Bag { n: i64, items: i64[>..<] }

var global_told = Told { notice: 1, a: 2 };

fn put(path: u8[>..], x: i64) { print(path, " = ", x); }
fn put(path: u8[>..], x: bool) { print(path, " = ", if x { 1 } else { 0 }); }
fn put(path: u8[>..], xs: i64[4]) { for x, i in xs { print(path, "[", i, "] = ", x); } }
fn put(path: u8[>..], s) { fields(s) { name, f => put(str(path, ".", name), f) } }

fn bump(x: i64&) { x = x + 1; }
fn bump(x: bool&) { x = !x; }
fn bump(xs: i64[4]&) { for &x in xs { x = x + 1; } }
fn bump(s) { fields(&s) { name, f => bump(f) } }

fn show(name: u8[:], x: i64) { print(name, ": ", x); }
fn show(name: u8[:], x: bool) { print(name, ": ", x); }
fn show(name: u8[:], xs: i64[4]) { print(name, ": ", xs); }
fn show(name: u8[:], t: Told) { print(name, ": Told ", t.notice, " ", t.a); }
fn show(name: u8[:], s: u8[:]) { print(name, ": ", s); }

fn is_set(x: i64) -> bool { x != 0 }
fn is_set(x: bool) -> bool { x }
fn is_set(xs: i64[4]) -> bool { xs[0] != 0 }
fn is_set(t: Told) -> bool { t.notice != 0 || t.a != 0 }

fn first_set(w: W) -> u8[] {
    fields(w) { name, f => if is_set(f) { return name; } }
    "none"
}

fn clear_at(ts: W[2]&, k: i64) { fields(&ts[k].told) { name, f => f = 0; } }

fn fill(n: i64&) { n += 1; }
fn fill(xs: i64[>..<]&) { xs.push(7); xs.push(8); }

fn main() {
    var w = W { turn: 7, asked: false, hp: [1, 2, 3, 4], told: Told { notice: 5, a: 9 } };
    bump(&w);
    put(str("W"), w);

    fields(w, show);
    w.fields() { name, f => show(name, f) }
    let r = &w;
    fields(&r.told) { name, f => f = f * 10; }
    fields(r.told) { name, f => print(name, " ", f) }
    fields(global_told) { name, f => print(name, " ", f) }

    var count = 0;
    let e = Empty {};
    fields(e) { name, f => count += 1; }
    let p = Padded { a: 1, b: 2 };
    fields(p) { name, f => count += 1; }
    print("fields of Empty and Padded: ", count);

    let pair = Pair<f64> { first: 0.5, second: 1.5 };
    fields(pair) { name, f => print(name, " = ", f) }

    let named = Named { name: "abc", n: 5 };
    fields(named, show);
    fields(&named) { name, f => print(name, " ", f) }
    var bag = Bag { n: 2, items: [] };
    fields(&bag) { name, f => fill(f) }
    fields(&bag) { name, f => print(name, " ", f) }

    var zero = W { turn: 0, asked: false, hp: [0, 0, 0, 0], told: Told { notice: 0, a: 0 } };
    print(first_set(zero));
    zero.told.a = 1;
    print(first_set(zero));

    var ws = [w, w];
    let i = 1;
    fields(&ws[i].told) { name, f => f = 0; }
    print(ws[1].told.a, " ", ws[0].told.a);
    clear_at(ws, 0);
    for x, j in ws { fields(ws[j].told) { name, f => print(j, " ", name, " ", f) } }

    var t = Told { notice: 1, a: 2 };
    fields(t) { name, f => print(name, " ", f); t = Told { notice: 10, a: 20 }; }

    fields(w.told) { outer, f => fields(w.told) { inner, g => print(outer, ".", inner) } }
}
