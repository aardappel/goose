// error: apply_contained()'s message cannot alias text, which is mutably partitioned by chunks()
// The message is appended once the tasks have ended, but a buffer that is
// also the array the tasks write is a mistake.
fn upper(chunk: u8[:]) {
    for &c in chunk { c = c - 32; }
}

fn main() {
    var pool = thread_pool(2);
    var text: u8[>..] = "abcd";
    pool.apply_contained(chunks(text, 2), text, upper);
}
