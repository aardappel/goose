// error: .index_of needs a reference rooted at the array itself
// error: this one is first, which free_ref takes as a parameter, from any root
// free_ref's body is checked once for every caller, so its reference
// parameter may point anywhere: the error names the program's argument.
import slot_pool;
fn main() {
    var pool = slot_pool<i64> {};
    let first = pool.alloc_ref(1);
    pool.free_ref(first);
}
