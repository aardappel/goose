// error: function split cannot return Node&<u16>?: a result is a value, never a relative reference
// Any of several results, and one written relative around a type variable
// as much as a concrete one: only a type argument that is itself relative
// makes a result the plain reference a load gives (§3.9).
struct Node { v: i64, next: Node&<u16>? }
fn split<T>(a: T[>..]&, i: i64) -> i64, T&<u16>? { return i, a[i].next; }
fn main() {
    var ns: Node[>..] = [];
    ns.push(Node { v: 1, next: null });
    ns.push(Node { v: 2, next: ns[0] });
    let k, n = split(ns, 1);
    print(k);
}
