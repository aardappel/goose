// error: .index_of takes a reference to an element of u8[:][>..], got u8[:]
// A bare block's value is a copy of its tail, a slice element included.
fn main() {
    var h: u8[>..] = [];
    h.append("abcdef");
    var views: (u8[:])[>..] = [];
    views.push(h[0..1]);
    views.push(h[1..2]);
    print(views.index_of({ views[1] }));
}
