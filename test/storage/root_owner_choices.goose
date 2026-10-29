// runtime-debug: stored, optional and branch-selected owners preserve field regions.
root struct S { first: i64[>..<], second: i64[>..<] }
struct Holder { s: S& }
fn head(s: S&) -> i64& { s.first[0] }
fn clear_second(s: S&) { s.second.clear(); }
fn select(a: S&, b: S&, which: bool) -> S& { if which { a } else { b } }
fn from_holder(h: Holder&) -> S& { h.s }
fn use(a: S&, b: S&) {
    let r .= a.first[0];
    b.second.clear();
    print(r);
}
fn main() {
    var a = S { [1], [10] };
    var b = S { [2], [20] };
    var holder = Holder { a };
    var refs: S&[2] = [a, b];
    var maybe: S&? = b;
    let one = from_holder(holder);
    clear_second(one);
    print(head(one));
    let chosen = select(a, b, false);
    let r .= head(chosen);
    clear_second(chosen);
    print(r);
    if maybe { print(maybe.first[0]); }
    print(refs[0].first[0], " ", refs[1].first[0]);
    use(a, a);
    use(a, b);
}
