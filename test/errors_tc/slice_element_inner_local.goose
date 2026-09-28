// error: storing a reference rooted at l, which does not outlive the destination
// An element of an outer array of slices assigned a view of a block's local
// (§3.10, §9.2).
fn main() {
    var parts: const u8[:][2] = ["a", "b"];
    {
        var l: u8[..4] = "l";
        parts[0] = l;
    }
    print(parts[0]);
}
