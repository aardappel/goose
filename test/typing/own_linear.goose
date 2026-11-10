// The linear rules of own values (docs/design/ownership.md 4.2-4.4): a
// variable holding one is consumed exactly once on every path, by a
// parameter declared own, a return, a declaration that takes it over or a
// defer that claims it; every other use borrows it as its plain type.
struct Node { v: i64 }
struct Window { id: u64, title: u8[..8] }

var nodes: Node[>..] = [];
var live = 0;
var windows = 0;

// A trusted fn is where the obligation begins and where it ends.
trusted fn make(v: i64) -> own Node& { live += 1; return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) { live -= 1; }
trusted fn open(id: u64) -> own Window { windows += 1; return Window { id: id, title: "w" }; }
trusted fn close(w: own Window) { windows -= 1; }
trusted fn raw_open() -> own u64 { return 41; }
trusted fn raw_close(h: own u64) {}

fn value(n: Node&) -> i64 { n.v }
fn title_len(w: Window&) -> i64 { w.title.len }
fn peek(x) -> i64 { x.v }
fn id<T>(x: T) -> T { x }
fn pass(h: own Node&) -> own Node& { h }

fn borrows() {
    let h = make(1);
    // Fields, reference parameters, untyped parameters and operators borrow.
    let r: Node& = h;
    let c = copy(h);
    print("borrows ", h.v, " ", value(h), " ", peek(h), " ", id(h).v, " ", r.v + c.v, " ", h == r);
    h.v = 5;
    print("written through ", r.v);
    drop_node(h);
}

fn moves() {
    let a = make(2);
    let b: own Node& = a;              // a move, written as one: a holds nothing from here on
    let c = pass(pass(b));
    var d: own Node& = c;
    drop_node(d);
    d .= make(3);                      // a var that holds nothing takes a new value
    print("moves ", d.v);
    drop_node(d);
}

fn branches(k: i64) -> i64 {
    let h = make(k);
    if k == 0 {
        drop_node(h);
        return 0;
    }
    let v = match k {
        1 => { drop_node(h); 10 },
        2, 3 => { let m: own Node& = h; drop_node(m); 20 },
        _ => { drop_node(h); 30 },
    };
    return v;
}

fn loops() {
    var total = 0;
    for i in 3 {
        let h = make(i);
        if i == 1 {
            drop_node(h);
            continue;
        }
        total += h.v;
        drop_node(h);
    }
    var cur = make(100);
    var n = 0;
    while n < 3 {
        drop_node(cur);
        cur .= make(100 + n);
        n += 1;
    }
    let found = loop {
        let h = make(n);
        n += 1;
        if n > 5 { break h; }
        drop_node(h);
    };
    print("loops ", total, " ", cur.v, " ", found.v);
    drop_node(cur);
    drop_node(found);
}

fn optionals(c: bool) {
    let h: own Node? = if c { make(7) } else { null };
    // The else of the test finds it null, which holds nothing.
    if h {
        print("optional ", h.v);
        drop_node(h);
    }
    let g: own Node? = make(8);
    guard g else { return; }
    drop_node(g);
}

fn pick(c: bool) -> own Node& {
    let a = make(1);
    let b = make(2);
    if c {
        drop_node(b);
        return a;
    }
    drop_node(a);
    b
}

fn two() -> i64, own Node& { return 9, make(9); }

fn claims() -> i64 {
    let h = make(4);
    defer drop_node(h);               // claimed: released at every exit
    if h.v == 4 { return value(h); }
    return 0;
}

fn each<F>(n: i64) { for i in n { F(i); } }
fn once<F>() { F(); }

fn blocks() -> i64 {
    let h = make(6);
    once() { drop_node(h); };          // a block the callee runs once may consume
    let g = make(60);
    defer drop_node(g);
    each(10) { if it == g.v / 10 { return it; } };   // a return through each: g is claimed
    return -1;
}

fn deep(n: i64) -> i64 { if n > 2 { return -1 from guarded; } n }
fn guarded(n: i64) -> i64 {
    let h = make(n);
    defer drop_node(h);
    return deep(n) + deep(n + 1);
}

fn nested() {
    let h = make(11);
    fn finish() { print("nested ", h.v); drop_node(h); }
    finish();
}

recursive fn chain(n: i64, h: own Node&) -> own Node& {
    if n == 0 { return h; }
    drop_node(h);
    return chain(n - 1, make(n));
}

fn handles() {
    let w = open(3);
    print("window ", w.id, " ", title_len(w), " ", w.title);
    close(w);
    let raw = raw_open();
    let plain: u64 = raw;              // an integer handle is borrowed by copy
    print("raw ", raw + 1, " ", plain);
    raw_close(raw);
}

fn main() {
    borrows();
    moves();
    print("branches ", branches(0), " ", branches(1), " ", branches(2), " ", branches(9));
    loops();
    optionals(true);
    optionals(false);
    let p = pick(true);
    let q = pick(false);
    print("pick ", p.v, " ", q.v);
    drop_node(p);
    drop_node(q);
    let n, h = two();
    print("two ", n, " ", h.v);
    drop_node(h);
    print("claims ", claims(), " blocks ", blocks(), " guarded ", guarded(1), " ", guarded(2));
    nested();
    drop_node(chain(3, make(0)));
    handles();
    print("live ", live, " windows ", windows);
}
