// bytes_of is a view of a live structure: writing through it would forge the
// relative references the checker otherwise proves (§3.9), so it is never
// writable however writable the array is.
struct Node { v: i32, next: Node&<u32>? }

fn main() {
    var pool: Node[>..] = [];
    pool.push(Node { v: 1 });
    let b = pool.bytes_of();
    b[0] = 99;
    print(b.len);
}
