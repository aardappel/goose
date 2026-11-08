// An untyped block parameter binds a non-fixed lvalue by reference, as an
// untyped function parameter does (§4.1), so one `F(xs)` works for a block
// and a named function alike. In generic code, a `&` that one instantiation
// needs and another has no need for is no warning.
root struct Mixed { items: i64[>..], code: i64 }

fn add_one(k) { k.push(1); }
fn run<F>() -> i64 {
    var xs: u8[>..] = [];
    F(xs);
    F(xs);
    return xs.len;
}
fn run_ref<F>() -> i64 {
    var xs: u8[>..] = [];
    F(&xs);
    return xs.len;
}
fn run_copy<F>() -> i64 {
    var xs: u8[>..] = [];
    F(copy(xs));
    return xs.len;
}
fn run_fixed<F>() -> i64 {
    var n = 5;
    F(n);
    return n;
}
fn run_two<F>() -> i64 {
    var a: i64[>..] = [];
    var m = Mixed { [], 0 };
    F(a, m.items);
    return a.len * 10 + m.items.len;
}
fn visit<T, F>(v: T&) {
    const for <N, A> in fields<T> { F(N, &v.A); }
}
fn show(name, field) { print(name, "=", field); }

fn main() {
    print(run(add_one), " ", run() { k => k.push(1); }, " ", run() { it.push(1); }, " ",
          run() { k: u8[>..]& => k.push(1); });
    print(run_ref(add_one), " ", run_ref() { it.push(1); });
    print(run_copy(add_one), " ", run_copy() { it.push(1); });
    print(run_fixed() { k => print(k + 1); });
    print(run_two() { a, b => a.push(1); b.push(2); b.push(3); });
    var m = Mixed { [1, 2], 3 };
    visit(m, show);
    visit(m) { name, field => print(name, ":", field); }
}
