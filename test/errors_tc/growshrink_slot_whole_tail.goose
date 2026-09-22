// error: cannot pop w.q.tail: an earlier expression value
// error: may still refer into w.q.tail
// w.q, read out of a field, leads to a struct whose tail is a grow-shrink
// array (§5.2): the slice of that tail passed first views what the pop
// frees.
struct S { n: i64, tail: i64[>..<] }
struct W { q: S& }
fn total(s: const i64[:], n: i64) -> i64 {
    var t = n;
    for x in s { t += x; }
    t
}
fn main() {
    var s = S { n: 1, tail: [4, 5] };
    var w = W { q: s };
    print(total(w.q.tail, w.q.tail.pop()));
}
