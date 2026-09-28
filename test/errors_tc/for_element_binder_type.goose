// error: an array's element binds at its element type
// A type on a for loop's binder is for an integer range or count (§6.5); an
// element converts, where it has to, with `as` in the body.
fn main() {
    var xs: u8[>..] = [1, 2, 3];
    for x: i32 in xs {
        print(x);
    }
}
