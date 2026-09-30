// error: cannot call f: it shrinks arr while words is still used, and words may refer into arr
// names holds a byte view of arr's elements (§5.1).
fn f(arr: i32[>..]&, words: (const u8[:])[:]) {
    arr.clear();
    arr.push(0x5A5A5A5A);
    arr.push(0x5A5A5A5A);
    arr.push(0x5A5A5A5A);
    print(words[0][0]);
}
fn main() {
    var arr: i32[>..] = [1, 2, 3];
    var names: (const u8[:])[>..] = [];
    names.push(bytes_of(arr[..]));
    f(arr, names[..]);
}
