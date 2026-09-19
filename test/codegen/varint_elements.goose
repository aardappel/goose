// runtime-debug: varint array elements (§3.3, §3.6) are written zigzag-
// encoded wherever an element is constructed and decode to i64 wherever one
// is read: iterated with and without an index, printed, compared, summed
// through a slice parameter, and returned by reference from a function and
// from push. An element's value comes from a literal, a local, a varint
// field, a for binding over another varint array, a call (inlined at -O2,
// one with an early return), an if choosing between a call and a constant,
// a function value's body, a returned varint reference and a fill value,
// which is evaluated once. It lands in literals of every length storage (a
// varint count past 127 elements included), struct and variant fields, a
// field's default, pushed and appended elements, a global, a frame object's
// tail and a whole assignment. to_bytes images pin the encoding, byte by
// byte, and from_bytes reads one back and rejects a truncated one.
struct Rec { id: u8, xs: varint[], q: varint }
struct Deflt { tag: u8, xs: varint[] = [1, -2] }
struct Tail { n: i64, xs: varint[>..] }
enum Shape { Pts { xs: varint[] }, Nothing }

var glob: varint[>..] = [];
var fills = 0;

fn counted(x: i64) -> i64 {
    fills++;
    x
}

fn sq(x: i64) -> i64 {
    var r = x * x;
    if r > 10 { r -= 1; }
    return r;
}

fn early(x: i64) -> i64 {
    if x < 0 { return -x * 100; }
    x + 1
}

fn apply<F>(n: i64) -> i64 { F(n) }

fn sum(xs: varint[:]) -> i64 {
    var s = 0;
    for x in xs { s += x; }
    return s;
}

fn firstof(xs: varint[:]) -> varint& {
    for x in xs { return x; }
    abort("empty");
}

fn squares(k: i64) -> varint[varint] {
    var r: varint[>..] = [];
    for i in k { r.push(i * i - 5000); }
    return r;
}

fn image(img: u8[:]) {
    var out: u8[>..] = [];
    for b, i in img {
        if i > 0 { out.push(' '); }
        format(out, b);
    }
    print(out);
}

fn literals() {
    var v: varint[>..] = [1, 300];
    print(v.len, " ", v);                    // 2 [1, 300]
    var p: varint[>..] = [];
    p.push(5);
    print(p.len, " ", p);                    // 1 [5]
    let r = Rec { id: 7, xs: [1, 300], q: -3 };
    print(r.xs.len, " ", r);                 // 2 Rec { 7, [1, 300], -3 }
    // 63 and -64 are the last one-byte values, 8191 and -8192 the last
    // two-byte ones.
    let edges: varint[] = [0, -1, 1, 63, -64, 64, -65, 8191, -8192, 8192];
    print(edges);
    image(to_bytes(edges));
    let wide: varint[u8] = [-9223372036854775807 - 1, 9223372036854775807];
    print(wide);
    image(to_bytes(wide));
    let vlen: varint[varint] = [-8, 8];
    let wlen: varint[u16] = [4096, -4097];
    print(vlen, " ", wlen);                  // [-8, 8] [4096, -4097]
    let many: varint[varint] = [-65; 130];
    var s = 0;
    for x in many { s += x; }
    print(many.len, " ", s, " ", to_bytes(many).len);   // 130 -8450 262
}

fn reads() {
    let v: varint[] = [3, -1, 1000, -9223372036854775807 - 1];
    for x, i in v { print(i, ": ", x); }
    var s = 0;
    for x in v { if x > -1000 { s += x; } }
    print(s);                                // 1002
    let w: varint[varint] = copy(v);
    let back: varint[] = copy(w);
    let other: varint[] = [3, -1, 1000, 5];
    print(w.len, " ", v == back, " ", v == other);   // 4 true false
}

fn sources(k: i64) {
    let r = Rec { id: 1, xs: [k, -k], q: k * 10 };
    var got: varint[>..] = [];
    for x in r.xs { got.push(x); }
    got.push(r.q);
    print(got);
    let calls: varint[] = [sq(k), early(k), early(-k), if k > 3 { sq(k) } else { -1 }];
    print(calls, " ", sum(calls));
    let fv: varint[] = [apply(k) { y => y * y }, apply(k) { y => if y > 1 { y * 100 } else { 0 } }];
    print(fv);
    let refs: varint[] = [firstof(r.xs), firstof(got) * 2];
    print(refs, " ", firstof(calls));
    let e .= got.push(-70);
    print(e, " ", got.push(80), " ", got);
    let filled: varint[] = [counted(-k); 3];
    print(filled, " ", fills);
}

fn destinations() {
    glob.push(-5);
    glob.append([500, 7]);
    print(glob);                             // [-5, 500, 7]
    var t = Tail { n: 2, xs: [3, -300] };
    t.xs.push(7);
    t.xs.append(glob);
    print(t.n, " ", t.xs);                   // 2 [3, -300, 7, -5, 500, 7]
    let d = Deflt { tag: 1 };
    print(d, " ", d == Deflt { tag: 1, xs: [1, -2] }, " ", d == Deflt { tag: 1, xs: [] });
    var shapes: Shape..[>..] = [];
    shapes.push(Shape.Pts { xs: [8, -8] });
    shapes.push(Shape.Nothing);
    for sh in shapes {
        match sh {
            Pts pt  => print("pts ", pt.xs),
            Nothing => print("nothing"),
        }
    }
    let nested: varint[][] = [[1, 2], [], [-3]];
    print(nested);
    let long = squares(200);
    var n = 0;
    var s = 0;
    for x in long { n++; s += x; }
    print(long.len, " ", n, " ", s);         // 200 200 1646700
    let short: varint[u16] = squares(130);
    s = 0;
    for x in short { s += x; }
    print(short.len, " ", s);                // 130 73905
    var u: varint[>..] = [1, 2, 3];
    u = [9, -9];
    print(u);                                // [9, -9]
    u.clear();
    u.push(4);
    print(u);                                // [4]
}

fn images() {
    var v: varint[>..] = [-5, 500];
    let img = to_bytes(v);
    image(img);                              // 3 9 232 7
    var back, ok = from_bytes<varint[>..]>(img);
    print(ok, " ", back, " ", back == v);    // true [-5, 500] true
    let cut: u8[] = [1, 128];
    var none, nok = from_bytes<varint[>..]>(cut);
    print(nok, " ", none.len);               // false 0
}

fn main() {
    literals();
    reads();
    sources(4);
    sources(-2);
    destinations();
    images();
}
