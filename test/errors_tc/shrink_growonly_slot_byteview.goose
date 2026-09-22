// error: cannot clear nums while t is still used: it may hold a reference or slice into it
// A byte view of a grow-only array may be stored in a field (§5.1), so a
// view read back out of one may point into nums; only a grow-shrink array's
// shrink passes over views read out of storage (§5.2).
struct Entry { text: const u8[:] }
fn main() {
    var nums: i64[>..] = [];
    nums.push(7);
    var entries: Entry[>..] = [];
    entries.push(Entry { text: nums.bytes_of() });
    let t = entries[0].text;
    nums.clear();
    print(t);
}
