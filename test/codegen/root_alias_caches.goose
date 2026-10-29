// runtime-debug: immutable root aliases share caches; mutable receivers are sampled.
root struct State { first: i64[>..], second: i64[>..] }
var global = State { .. };
inline fn append(s: State&, i: i64) {
    let alias .= s;
    let again .= alias;
    again.first.push(i);
    again.second.push(2 * i);
}
// Multiple results preserve the Goose call boundary for the C-shape check.
fn grow(s: State&, n: i64) -> i64, i64 {
    for i in n { append(s, i); }
    return s.first.len, s.second.len;
}
fn mixed(s: State&, t: State&, a: i64[>..]&) {
    for i in 3 { append(s, i); append(t, 10 + i); a.push(20 + i); }
}
fn touch(s: State&, i: i64) -> i64, i64 {
    append(s, i);
    append(global, 100 + i);
    return s.first.len, s.second.len;
}
fn rounds(s: State&, n: i64) -> i64, i64 {
    for i in n { append(s, i); touch(s, 10 + i); }
    return s.first.len, s.second.len;
}
fn main() {
    var a = State { .. };
    var b = State { .. };
    let alias .= a;
    let again .= alias;
    grow(again, 4);
    mixed(alias, a, again.first);
    print(a.first, " ", a.second);
    // The alias must retain the old value when its source is rebound.
    var r .= a;
    let saved .= r;
    r .= b;
    append(saved, 40);
    append(r, 50);
    fn next() -> i64 { r .= a; 60 }
    append(r, next());
    print(a.first, " ", b.first);
    // Passing an owner to a nested call flushes both cached components.
    rounds(again, 3);
    let g .= global;
    grow(g, 2);
    print(a.first, " ", global.first);
}
