// error: .index_of needs a reference rooted at the array itself
// error: this one is value, which free_ref takes as a parameter, from any root
import slot_pool;
struct Indices { a: u32 in first.items, b: u32 in second.items }
var first = slot_pool<i64> {};
var second = slot_pool<i64> {};
fn release(p: slot_pool<i64>&, value: i64&) { p.free_ref(value); }
fn main() {
    let a: i64& = first.alloc_ref(1);
    let b: i64& = second.alloc_ref(2);
    let indices = Indices { 0 as u32 in first.items, 0 as u32 in second.items };
    release(first, a);
    release(second, a);
    print(indices.a, b);
}
