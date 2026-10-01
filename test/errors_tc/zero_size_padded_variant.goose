// error: an element cannot be a variant type with no fields: Gap.Hole
// A pad without a size aligns only a field after it (§3.2): a variant with
// nothing else takes no bytes either, and is no element (§3.4).
enum Gap { Hole { pad }, Fill { v: i64 } }

fn main() {
    var holes: Gap.Hole[>..] = [];
    holes.push(Gap.Hole {});
    print(holes.len);
}
