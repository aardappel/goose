// A global list of strings can never be viewed as bytes, so another global
// holding string slices does not keep it from being cleared, and a string
// read out of it survives the clear.
var keywords: const u8[:][>..] = ["alpha", "beta"];
var names: const u8[:][>..] = ["gamma"];
fn main() {
    let n = names[0];
    names.clear();
    names.push("delta");
    print(n, " ", names[0], " ", keywords[1]);
}
