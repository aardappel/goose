// error: a relative reference must point within the same root as its location
// error: it was read out of s and may point into pool or spare
// A reference handed to a nested function can point into a local its
// declaration does not see, here one declared further on that pick returns
// into: a read-back in the body, out of a copy of an element of slots, counts
// it among what the reference may point into (§9.5), so the link is not one within spare (§3.9).
struct Node { key: i32, next: Node&<u32>? }
struct S { n: Node&? }
fn main() {
    var spare: Node[>..] = [];
    spare.push(Node { key: 9 });
    fn link() {
        var slots: S[>..] = [];
        slots.push(S { n: pick() });
        let s = slots[0];
        let n = s.n;
        assert(n);
        spare[0].next .= n;
    }
    var pool: Node[>..] = [];
    pool.push(Node { key: 1 });
    fn pick() -> Node& { pool[0] }
    link();
    print(spare[0].key);
}
