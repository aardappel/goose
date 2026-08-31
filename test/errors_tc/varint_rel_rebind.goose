// varint-width relative references are written only at construction (§3.9).
struct Node2 { v: i64 = 0, link: i64&<varint>? }
fn main() {
    var pool: Node2[>..] = [];
    let a: Node2& = pool.push(Node2 { v: 1 });
    let b: Node2& = pool.push(Node2 { v: 2, link: &a.v });
    b.link .= &a.v;
    print(b.v);
}
