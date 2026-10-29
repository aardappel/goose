// runtime-debug: recursive borrowing, fixed-only recursive owners, callbacks and relative links.
struct Node { value: i64, next: Node&<u32>? }
root struct S { nodes: Node[>..], scratch: i64[>..<] }
recursive fn work(s: S&, n: i64) {
    if n == 0 { return; }
    s.scratch.push(n);
    work(s, n - 1);
    s.scratch.pop();
}
fn callback<F>(s: S&) { F(s); }
root struct Counter { n: i64 }
recursive fn count(n: i64) -> i64 {
    var s = Counter { n };
    if n == 0 { return s.n; }
    return s.n + count(n - 1);
}
fn early() -> i64 {
    var s = S { .. };
    callback(s) {
        var inner = S { .. };
        inner.scratch.push(99);
        return inner.scratch[0];
    };
    return 0;
}
fn main() {
    var s = S { .. };
    let first: Node& = s.nodes.push(Node { 1, null });
    s.nodes.push(Node { 2, first });
    work(s, 5);
    callback(s) { a => a.scratch.push(7); };
    let next = s.nodes[1].next;
    if next { print(next.value, " ", s.scratch, " ", count(4)); }
    print(early(), " ", early());
}
