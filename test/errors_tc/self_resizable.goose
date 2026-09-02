// A relative reference is an offset alone, so it cannot reach a resizable
// value's header (§3.9). Every other relative reference is kept away from one
// by the same-root rule; a self-reference satisfies that rule by construction,
// so it is rejected here.
struct Grow { me: Grow&<u32>, xs: i64[>..] }
fn main() {
    var g: Grow = Grow { me: self, xs: [] };
    print(g.xs.len);
}
