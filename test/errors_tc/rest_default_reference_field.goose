// error: missing initializer for field owner of Item (it has no declared default, and Unit& is a non-optional reference)
// A literal's `..` fills a field in with its type's default value (§4.2),
// which a non-optional reference does not have.
struct Unit { hp: i32 }
struct Item { kind: i64, owner: Unit&, weight: f32 = 1.0 }

fn main() {
    let item = Item { kind: 1, .. };
    print(item.kind);
}
