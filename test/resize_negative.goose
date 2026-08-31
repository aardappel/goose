// Resize to a negative length is a runtime abort (§9.3): it shrinks past
// empty, corrupting a grow-shrink array's stack top or a limited array's
// stored length exactly as an unchecked pop on empty would.

fn main_resize_negative() {
    var a: i64[>..<] = [1, 2, 3];
    a.resize(1);
    print(a.len);                        // 1
    a.resize(a.len - 2);                 // Aborts.
}

fn main() { main_resize_negative(); }
