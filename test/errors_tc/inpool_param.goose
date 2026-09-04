// Nor a parameter: the pool is named where the field is declared, and a
// parameter's name means nothing there (§3.9).
struct PNode { key: i32, next: PNode&<u32 in p>? }
fn pfill(p: PNode[>..]&) { p.push(PNode { key: 1 }); }
fn main() {
    var pool: PNode[>..] = [];
    pfill(&pool);
    print(pool.len);
}
