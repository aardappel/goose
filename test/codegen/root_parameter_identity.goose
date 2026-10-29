// runtime-debug all-levels
root struct State { a: i64[>..], b: i64[>..], count: i64 }
struct Holder { owner: State& }

fn roundtrip(p: State&, h: Holder&) -> State&, i64 {
    assert(p .== h.owner);
    p.a.push(10);
    h.owner.a.push(20);
    p.b.push(30);
    p.count++;
    let optional: State&? = p;
    if optional { assert(optional .== h.owner); }
    fn captured() -> i64 { p.a.push(40); p.count }
    return p, captured();
}
fn rebind(var p: State&, q: State&) -> State&, i64 {
    p.a.push(50);
    p .= q;
    p.a.push(60);
    p.count += 2;
    return p, p.count;
}
recursive fn walk(p: State&, h: Holder&, n: i64) -> State& {
    assert(p .== h.owner);
    p.b.push(n);
    if n > 0 { return walk(p, h, n - 1); }
    return p;
}
fn main() {
    var a = State { .. };
    var b = State { .. };
    var ha = Holder { a };
    var hb = Holder { b };
    let ar, ac = roundtrip(a, ha);
    let br, bc = roundtrip(b, hb);
    assert(ar .== &a && br .== &b && ar .!= br && ac == 1 && bc == 1);
    assert(walk(a, ha, 3) .== &a && walk(b, hb, 2) .== &b);
    let changed, count = rebind(a, b);
    assert(changed .== &b && count == 3 && a.count == 1);
    assert(a.a == [10, 20, 40, 50] && b.a == [10, 20, 40, 60]);
    assert(a.b == [30, 3, 2, 1, 0] && b.b == [30, 2, 1, 0]);
    print("root parameter identity: ok");
}
