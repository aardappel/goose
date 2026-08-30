// alloc/free exist on `reusable` pools only (§5.4).
struct Slot { v: int = 0 }
fn main() {
    var pool: Slot[>..] = [];
    let i = pool.alloc_index(Slot { v: 1 });
    print(i);
}
