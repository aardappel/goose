// error: cannot infer the element type of []: only a var whose whole initializer is [] takes it from what goes into it first
// A construct whose branches are all [] is still one (§6.4), which an
// un-annotated let has no element type for (§4.2).
fn main() {
    let c = true;
    let x = if c { [] } else { [] };
    print(x.len);
}
