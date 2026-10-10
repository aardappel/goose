// Holders that only ever held null links hold nothing: they may be
// returned from the function declaring them, and an array may shrink while
// they are live (§9.2, §9.5).
struct B { v: f64 }
struct N { v: f64, link: B? }

fn one() -> N {
    var x = N { v: 1.0, link: null };
    x.v += 1.0;
    return x;
}

fn many(n: i64) -> N[>..] {
    var xs: N[>..] = [];
    for i in n { xs.push(N { v: i, link: null }); }
    return xs;
}

fn main() {
    let a = one();
    let m = many(3);
    print(a.v, " ", m.len, " ", m[2].v);
    var data: B[>..] = [B { v: 1.0 }, B { v: 2.0 }];
    var x = N { v: 4.0, link: null };
    data.pop();
    print(x.v, " ", data.len);
}
