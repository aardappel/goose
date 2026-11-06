// chunks(data, offsets) cuts data at each offset, so the offsets may not fall
// or leave data: the first chunk here is fine, the second would end past
// data's last element.
fn fill(chunk: i64[:], idx: i64) {
    for &x in chunk { x = idx; }
}

fn main() {
    var pool = thread_pool(2);
    var data: i64[>..] = [0, 0, 0, 0];
    let bad_offsets: i64[3] = [0, 2, 5];
    pool.apply(chunks(data, bad_offsets), fill);
    print(data);
}
