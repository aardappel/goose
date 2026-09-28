// runtime-debug: a grow-only array shrinks inside a larger expression wherever nothing the statement evaluated earlier, and uses later, refers into it (§5.1).
struct Item { name: u8[], n: i64 }
struct Pair { a: i64, b: i64 }

var items: Item[>..] = [];
var ns: i64[>..] = [];
var g: i64[>..] = [1, 2];

// Loads a saved state: both globals are cleared and refilled.
fn restore(n: i64) -> bool {
    items.clear();
    ns.clear();
    for i in n {
        ns.push(i * 10);
        items.push(Item { name: "item", n: i });
    }
    return n > 0;
}

fn wrap(n: i64) -> bool { return restore(n); }
fn keep(x: i64, ok: bool) -> i64 {
    if ok { return x; }
    return -1;
}
fn drop(a: i64[>..]&) -> i64 {
    a.clear();
    return 0;
}
fn drop_g() -> i64 {
    g.clear();
    return 0;
}
fn trim(a: i64[>..]&, n: i64) -> i64 {
    a.resize(n);
    return a.len;
}
fn plus(x: i64, y: i64) -> i64 { return x + y; }
fn run<F>() -> i64 { return F(); }
fn pops(a: i64[>..]&) -> i64 { return a.pop() * 100 + a.pop(); }

fn main() {
    // A callee that clears globals, in the conditions and arguments of the
    // statements that call it.
    if !restore(3) { print("fail"); }
    assert(wrap(2));
    print("loaded: ", restore(1), " ", ns.len, " ", items.len);
    let ok = restore(2) && wrap(4);
    print(ok, " ", ns.len, " ", items.len);
    // An operand read by value before the callee shrinks is not held.
    print(keep(ns[3], restore(1)), " ", ns.len);
    // Direct pops of a local, in any operand position.
    var a: i64[>..] = [1, 2, 3, 4, 5, 6];
    let s = a.pop() + a.pop();
    print(s, " ", a.len);
    print(a.pop(), " ", a.len);
    a.push(a.pop() * 10);
    print(a);
    print(plus(a[0], a.pop()), " ", a.len);
    // Inside the values of an if, a match, a block, a loop and a function
    // value.
    let c = a.len > 1;
    let v1 = if c { a.pop() } else { -1 };
    let k = a.len;
    let v2 = match k { 1 => a.pop(), _ => -2 };
    print(v1, " ", v2, " ", a.len);
    a.push(7);
    let v3 = { a.clear(); a.len };
    a.push(8);
    let v4 = loop { break a.pop(); };
    a.push(9);
    let v5 = run() { drop(a) };
    // A callee first checked inside the block, shrinking a global.
    let v6 = run() { drop_g() };
    print(v3, " ", v4, " ", v5, " ", a.len, " ", v6, " ", g.len);
    // A literal whose later field shrinks after an earlier one is copied.
    a.push(11);
    a.push(12);
    let p = Pair { a: a[1], b: a.pop() };
    print(p.a, " ", p.b, " ", a.len);
    // The heads of control constructs, whose parts name the array itself.
    a.push(13);
    if drop(a) == 0 { print("empty: ", a.len); }
    a.push(14);
    match a.pop() {
        14 => print("popped 14, ", a.len, " left"),
        _ => print("other"),
    }
    for i in trim(a, 0) + 2 { a.push(i); }
    print(a);
    // A loop around them.
    var total = 0;
    for i in 4 {
        if wrap(i) { total += ns[ns.len - 1]; }
    }
    print(total);
    while ns.len > 0 && ns.pop() > 0 { total += 1; }
    print(total, " ", ns.len);
    // An index after a shrink in the same expression checks the new length.
    var b: i64[>..] = [5, 6, 7, 8, 9];
    let x = trim(b, 3) + b[2]; // bce:keep
    print(x);
    print(pops(b), " ", b.len);
    // Built in place in another array than the one shrunk.
    var names: Item[>..] = [];
    names.push(Item { name: "n", n: drop(b) });
    names.append([Item { name: "m", n: trim(b, 0) }]);
    for it in names { print(it.name, " ", it.n); }
    print(names.len, " ", b.len);
    // A grow-shrink array's pushed element that is no view is evaluated
    // before it is added.
    var gs: i64[>..<] = [1, 2, 3];
    gs.push(gs.pop() * 10);
    if gs.pop() > 0 { print(gs); }
}
