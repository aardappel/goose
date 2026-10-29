// runtime-debug: independent descriptor caches and conservative alias fallbacks.
root struct S { first: i64[>..], second: i64[>..] }
var global = S { .. };
fn grow(s: S&, n: i64) {
    for i in n { s.first.push(i); s.second.push(2 * i); }
}
fn sum(s: S&) -> i64 {
    var n = 0;
    for i in s.first.len { n += s.first[i]; }
    return n;
}
fn aliases(s: S&, t: S&, n: i64) {
    for i in n { s.first.push(i); t.first.push(10 + i); }
}
fn mixed(s: S&, a: i64[>..]&) {
    for i in 3 { s.first.push(i); a.push(20 + i); }
}
fn via_global(s: S&) { aliases(s, global, 3); }
fn main() {
    var s = S { .. };
    grow(s, 4);
    print(sum(s), " ", s.second);
    aliases(s, s, 3);
    mixed(s, s.first);
    print(s.first);
    via_global(global);
    print(global.first);
}
