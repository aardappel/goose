// error: cannot use s.items in the value assigned to s.items
// Through a reference as on a local, the right-hand side is built over the
// old elements, so it may not use them (§4.4).
struct S { n: i64, items: i64[>..] }
fn twice(xs: const i64[:]) -> i64[>..] {
    var r: i64[>..] = [];
    r.append(xs);
    r.append(xs);
    return r;
}
fn grow(s: S&) { s.items = twice(s.items); }
fn main() {
    var s = S { n: 3, items: [1, 2] };
    grow(s);
    print(s.items);
}
