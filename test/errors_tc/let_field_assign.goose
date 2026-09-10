// A let field is not assigned after construction (§4.4).
struct Rec { let id: i64, n: i64 }
fn main() {
    var r = Rec { id: 1, n: 2 };
    r.id = 3;
}
