// error: in gpool: a relative reference's pool must be a grow-only resizable global (gpool: T[>..]), not GNode[>..<]
// A grow-shrink pool would move nothing but admits no interior references at
// all (§5.2), so its base is no place to measure offsets from (§3.9).
struct GNode { key: i32, next: GNode&<u32 in gpool>? }
var gpool: GNode[>..<] = [];
fn main() {
    gpool.push(GNode { key: 1 });
    print(gpool.len);
}
