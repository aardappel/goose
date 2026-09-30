// error: .index_of takes a reference to an element of u8[:][>..], got u8[:]
// The value of an if with one branch reaching is a copy of the element that
// branch names, a slice as much as any other (§4.1): not the element, whose
// index index_of could give (§3.3).
fn main() {
    var h: u8[>..] = [];
    h.append("abcdef");
    var views: (u8[:])[>..] = [];
    views.push(h[0..1]);
    views.push(h[1..2]);
    let c = views.len > 1;
    print(views.index_of(if c { views[1] } else { abort("no") }));
}
