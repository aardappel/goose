// runtime-debug all-levels
import slice_pool;
struct Held { r: i64& }
fn main() {
    var text = slice_pool<u8> {};
    let a = text.alloc_copy("abcdef");
    text.free_slice(a);
    let b = text.alloc_copy(a[1..5]);
    assert(b == "bcde" && text.items.range_index(b) == 0 && text.items.len == 6);
    let empty = text.alloc_copy(default<u8[:]>());
    assert(empty.len == 0 && text.items.len == 6);

    var up = slice_pool<i64> {};
    let original = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
    let whole = up.alloc_copy(original[..]);
    up.free_slice(whole[2..8]);
    let moved = up.alloc_copy(whole[..6]);
    assert(up.items.range_index(moved) == 2 && moved == [0, 1, 2, 3, 4, 5]);

    var tail = slice_pool<i64> {};
    let old = tail.alloc_copy(original[..6]);
    tail.free_slice(old[3..]);
    let extended = tail.alloc_copy(old);
    assert(tail.items.range_index(extended) == 3 && extended == original[..6]);
    assert(tail.items.len == 9);

    var values = [10, 20];
    var held = [Held { values[0] }, Held { values[1] }];
    var refs = slice_pool<Held> {};
    let copied = refs.alloc_copy(held[..]);
    assert(copied[0].r .== values[0] && copied[1].r .== values[1]);
    refs.free_slice(copied);
    let again = refs.alloc_copy(held[..]);
    assert(again[1].r == 20);
    print("slice pool copy: ok");
}
