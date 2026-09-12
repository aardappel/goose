// Slices read out of a global list of strings are not byte views: nothing
// can view a container of slices, so shrinking the list leaves them alone.
var names: const u8[:][>..<] = ["a", "b"];
struct Entry { name: const u8[:] }
fn main() {
    var entries: Entry[>..] = [];
    entries.push(Entry { name: names[0] });
    names.pop();
    print(entries.len, " ", entries[0].name);
    let m = names[0];
    names.pop();
    print(m, " ", names.len);
}
