// A resizable behind a variable-size prefix is not a frame object (C.2):
// it has no header of its own to reference (a fixed prefix would).
struct Named { name: u8[], items: i64[>..] }
fn main() {
    var v = Named { name: "x", items: [1] };
    let r = &v.items;
    r.push(2);
}
