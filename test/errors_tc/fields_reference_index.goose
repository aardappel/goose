// error: the index k of ts[k] is a reference
// Each field's call names ts[k] again (§12): a block changing the integer
// k refers to would take the next field from another element, so the index
// may not be a reference, even one that is not a var.
struct T { a: i64, b: i64, c: i64 }
fn walk(ts: T[3]&, k: i64&) { fields(&ts[k]) { name, f => f = 0; k += 1; } }
fn main() {
    var ts = [T { a: 1, b: 1, c: 1 }, T { a: 2, b: 2, c: 2 }, T { a: 3, b: 3, c: 3 }];
    var i = 0;
    walk(ts, i);
}
