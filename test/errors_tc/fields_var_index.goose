// error: the index i of ts[i] is a var
// Each field's call names ts[i] again (§12): a block changing i would take
// the next field from another element, so the index may not be a var.
struct T { a: i64, b: i64, c: i64 }
fn main() {
    var ts = [T { a: 1, b: 1, c: 1 }, T { a: 2, b: 2, c: 2 }, T { a: 3, b: 3, c: 3 }];
    var i = 0;
    fields(&ts[i]) { name, f => f = 0; i += 1; }
}
