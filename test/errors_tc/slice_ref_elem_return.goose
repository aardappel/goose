// error: returning a reference rooted in views, which dies with this function
// An element of an array of slices, bound by reference at a returned
// `u8[:]&`, is that element of f's local array (§3.8, §4.1).
var src: u8[>..] = [];
fn first() -> u8[:]& {
    var views: (u8[:])[>..] = [];
    views.push(src[0..2]);
    return views[0];
}
fn main() {
    src.append("abcdef");
    print(first());
}
