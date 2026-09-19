// error: cannot use x in the value assigned to x: that value is built over the old contents of x
// Reversing in place would read elements the new contents already
// replaced: the old ones are gone once the assignment starts (§4.4).
fn main() {
    var x: u8[>..] = [];
    x.append("abcd");
    x = [x[3], x[2], x[1], x[0]];
    print(x);
}
