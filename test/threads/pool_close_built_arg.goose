// An apply's arguments may close its pool (pool_close_chunk_size.goose):
// here a resizable extra argument, which the apply builds into the packet
// of each chunk it hands out, closes it as the first chunk's is built.
fn fill(chunk: i64[:], tag: u8[>..]) {
    for &x in chunk { x = tag.len; }
}

fn close_then_tag(p: thread_pool&) -> u8[>..] {
    p.close();
    return "ab";
}

fn main() {
    var p = thread_pool(2);
    var xs: i64[>..] = [0, 0, 0, 0];
    print("applying");
    p.apply(chunks(xs, 2), fill, close_then_tag(p));
    print(xs);
}
