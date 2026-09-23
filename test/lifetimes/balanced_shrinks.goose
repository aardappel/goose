// A call that leaves a grow-shrink array no shorter than it found it at any
// point is balanced (§5.2): every shrink of the array in it resizes it back
// to a `let` of its length taken in the same activation, or is a balanced
// call. Views taken before such a call stay in use across it: into a
// parameter's array, one array passed as two parameters, a struct's tail, a
// global, through a balanced callee of a balanced callee, and beside a pop
// of another array the caller owns. So do references to a slice variable
// viewing the array, held in a variable, held as an earlier argument, or
// handed to the callee: no callee can store a view of the array into that
// variable, so it still holds the slice taken before the call. And so does a
// parameter's view, whatever the caller passes for it: a balanced call
// records nothing for the callers to judge (§5.1).
struct Buf { n: i64, items: i64[>..<] }

var scratch: i64[>..<] = [];

fn helper(s: i64[>..<]&) {
    let mark = s.len;
    s.push(7);
    s.resize(mark);
}

fn both(a: i64[>..<]&, b: i64[>..<]&) {
    let ma = a.len;
    a.push(1);
    let mb = b.len;
    b.push(2);
    b.resize(mb);
    a.resize(ma);
}

fn outer(s: i64[>..<]&) -> i64 {
    let mark = s.len;
    s.push(20);
    let mine = s[mark..];
    helper(s);
    let got = mine[0];
    s.resize(mark);
    got
}

fn tail(b: Buf&) {
    let mark = b.items.len;
    push(b.items, 4);
    resize(b.items, mark, 0);
}

fn global_scratch() {
    let mark = scratch.len;
    for i in 3 {
        let inner = scratch.len;
        scratch.push(i);
        scratch.push(i);
        scratch.resize(inner);
    }
    scratch.push(5);
    scratch.resize(mark);
}

fn viewer(v: i64[:]) -> i64 {
    global_scratch();
    v[0]
}

fn other(s: i64[>..<]&, w: i64[>..<]&) {
    let mark = s.len;
    s.push(4);
    w.pop();
    s.resize(mark);
}

fn counted(s: i64[>..<]&) -> i64 {
    let mark = s.len;
    s.push(8);
    s.resize(mark);
    mark
}

fn first(v: i64[:]&, unused: i64) -> i64 { v[0] }

fn peek(s: i64[>..<]&, v: i64[:]&) -> i64 {
    let got = v[0];
    let mark = s.len;
    s.push(got);
    s.resize(mark);
    got
}

fn main() {
    var s: i64[>..<] = [];
    s.push(1);
    let mine = s[..];
    helper(s);
    print(mine[0], " ", s.len);
    both(s, s);
    print(mine[0], " ", s.len);
    print(outer(s), " ", mine[0], " ", s.len);
    var b = Buf { 0, [9] };
    let items = b.items[..];
    tail(b);
    print(items[0], " ", b.items.len);
    scratch.push(3);
    let g = scratch[..];
    global_scratch();
    print(g[0], " ", scratch.len);
    var w: i64[>..<] = [6, 6];
    other(s, w);
    print(mine[0], " ", s.len, " ", w.len);
    var view = s[..];
    let explicit = &view;
    let typed: i64[:]& = view;
    helper(s);
    print(explicit[0], " ", typed[0]);
    print(first(view, counted(s)), " ", peek(s, &view), " ", view.len);
    print(viewer(scratch[..]), " ", scratch.len);
}
