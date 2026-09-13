// Nothing can view a list of strings as bytes, so a string copied out of a
// grow-only global list into a local one does not keep the global from
// shrinking, and the copy survives it.
var names: const u8[:][>..] = ["a", "b"];
struct Entry { name: const u8[:] }
fn main() {
    var entries: Entry[>..] = [];
    entries.push(Entry { name: names[1] });
    names.pop();
    print(entries[0].name, " ", names.len);
}
