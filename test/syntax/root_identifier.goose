// The root modifier is contextual: it does not reserve existing identifiers.
struct root { root: i64 }

fn read(root: i64) -> i64 { root }
fn names::root(value: i64) -> i64 { value }
fn identity<root>(value: root) -> root { value }

fn main() {
    var root = root { root: 7 };
    print(read(root.root), " ", names::root(identity(root.root)));
}
