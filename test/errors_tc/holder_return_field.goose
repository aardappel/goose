// A holder field read out of a struct parameter is bounded by the struct, not
// rooted in it: the array its reference points into may not shrink while the
// returned copy is still used.
struct Holder { value: i64& }
struct Pair { h: Holder, k: i64 }
fn get(p: Pair&) -> Holder { return p.h; }
fn main() {
    var data: i64[>..] = [1, 2, 3];
    var p = Pair { h: Holder { value: data[2] }, k: 1 };
    let h = get(p);
    print(h.value);
    data.pop();
    data.push(99);
    print(h.value);
}
