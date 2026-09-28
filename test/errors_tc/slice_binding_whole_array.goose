// error: expected a value of type const u8[:], got u8[..16]
// error: slice it whole: name[..]
// A binding views an array only through a slice spelled out: calls and
// branch joins are where an array is a slice of itself (§3.10, §6.4).
fn main() {
    var name: u8[..16] = "anna";
    let s: const u8[:] = name;
    print(s);
}
