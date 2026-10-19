// error: storing a reference rooted at k, which does not outlive the destination
// A variant that does hold a reference, passed as its enum, still stores
// only where what it refers to outlives (§9.2), though one that holds
// nothing stores anywhere (holder_variant_argument.goose).
enum Kind { Leaf, Ref { r: const i64& } }
var kinds: Kind[>..] = [];
fn keep(k: Kind) { kinds.push(k); }
fn main() {
    let x = 5;
    keep(Kind.Leaf {});
    keep(Kind.Ref { r: x });
    print(kinds.len);
}
