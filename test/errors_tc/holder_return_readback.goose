// A returned holder's contents keep the exactness they were read back with.
struct Holder { value: i64& }
fn pass(hs: Holder[>..]&) -> Holder {
    let h = hs[0];
    return h;
}
fn main() {
    var data: i64[>..] = [1, 2, 3];
    var hs: Holder[>..] = [];
    hs.push(Holder { value: data[2] });
    let h2 = pass(hs);
    print(h2.value);
    data.pop();
    data.push(99);
    print(h2.value);
}
