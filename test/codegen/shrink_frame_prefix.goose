// A frame object's prefix fields are not element storage of its tail, also
// when reached through a reference to the whole frame object.
struct StackFo { top: i64, items: i64[>..<] }
struct Outer { id: i64, inner: StackFo }
fn pop_into(s: StackFo&) { s.top = s.items.pop(); }
fn main() {
    var s = StackFo { top: 0, items: [1, 2, 3] };
    s.top = s.items.pop();
    print(s.top, " ", s.items);
    var o = Outer { id: 0, inner: StackFo { top: 0, items: [4, 5] } };
    o.inner.top = o.inner.items.pop();
    o.id = o.inner.items.pop();
    print(o.id, " ", o.inner.top, " ", o.inner.items);
    var t = StackFo { top: 0, items: [6, 7] };
    pop_into(t);
    print(t.top, " ", t.items);
}
