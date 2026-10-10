// heap_pop on an empty heap aborts at its read of the root, before the array
// is touched; the sifts never run on an empty or emptied heap.
import std;

fn main() {
    var h: i64[>..<] = [];
    heap_push(h, 4);
    heap_push(h, 1);
    print(heap_pop(h), heap_pop(h), " ", h.len);   // 14 0
    print(heap_pop(h));                            // Aborts.
}
