// error: reordering may cross a side effect, runtime check, or nonlocal exit
// A relative offset is checked when stored, even though the initializer is a read.
struct Link { value: i64, prev: Link&<u8>? }
fn main() {
    var chain: Link[>..] = [];
    let first: Link& = chain.push(Link { value: 1 });
    chain.push(Link { prev: first, value: 2 });
}
