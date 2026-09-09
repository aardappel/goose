// One declaration per type name within a namespace; the same leaf in another
// namespace is a different name.
struct a::P { x: i64 }
struct P { x: i64 }
struct a::P { y: i64 }
fn main() {}
