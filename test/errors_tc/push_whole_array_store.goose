// error: storing a reference rooted at local, which does not outlive the destination
// An array pushed into an array of slices is a whole-array slice of itself
// (§3.10), stored as any slice is (§9.2).
fn main() {
    var parts: u8[:][>..] = [];
    {
        var local: u8[4] = [1, 2, 3, 4];
        parts.push(local);
    }
    print(parts.len);
}
