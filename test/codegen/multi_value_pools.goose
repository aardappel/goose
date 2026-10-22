// Several pools declared from one call's results are pools each, with a
// freelist of their own (§5.4), as one declared alone is: `reusable` slot
// pools reuse the slots they free, `reusable[]` slice pools the runs, which
// an allocation fills with default values.

struct Slot { n: i64 }

fn two() -> Slot[>..], Slot[>..] {
    var a: Slot[>..] = [];
    var b: Slot[>..] = [];
    a.push(Slot { n: 4 });
    b.push(Slot { n: 5 });
    b.push(Slot { n: 6 });
    return a, b;
}

fn runs() -> i64[>..], i64[>..] {
    var a: i64[>..] = [1, 2];
    var b: i64[>..] = [];
    return a, b;
}

fn slots() {
    reusable var p, q = two();
    let i = p.alloc_index(Slot { n: 1 });
    let j = q.alloc_index(Slot { n: 2 });
    p.free(0);
    q.free(j);
    q.free(0);
    let k = p.alloc_index(Slot { n: 3 });
    let r .= q.alloc_ref(Slot { n: 7 });
    let m = q.alloc_index(Slot { n: 8 });
    print(i, " ", j, " ", k, " ", q.index_of(r), " ", m, " ", p.len, " ", q.len);   // 1 2 0 0 2 2 3
    for s in p { print(s.n); }                                                      // 3, 1
    for s in q { print(s.n); }                                                      // 7, 6, 8
}

fn slices() {
    reusable[] var s, t = runs();
    let a = s.alloc_slice(3);
    a[0] = 10;
    s.free_slice(a);
    let b = s.alloc_slice(2);
    let c = t.alloc_slice(2);
    c[1] = 20;
    print(s.index_of(b[0]), " ", b[0], " ", s.len, " ", t.len, " ", c[1]);          // 2 0 5 2 20
}

fn main() {
    slots();
    slices();
}
