// A `const` global the static initializer cannot spell out -- a fill or a
// string past 256 elements (MAXSTATICELEMS) -- is a C static of its own,
// filled once before main and read in place by every worker (§11.2), where
// a `var` global is copied into each worker's instance at spawn.
const TABLE: u32[300] = [7; 300];
const ZEROS: i64[1000] = [0; 1000];
const TEXT: u8[260] = "01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";

thread_fn worker(i: i64) {
    var s: i64 = 0;
    for j in TABLE.len { s += TABLE[j]; }
    var z: i64 = 0;
    for j in ZEROS.len { z += ZEROS[j]; }
    var t: i64 = 0;
    for j in TEXT.len { t += TEXT[j]; }
    print("worker ", i, ": ", s, " ", z, " ", t);
}

fn main() {
    thread_wait(thread_spawn(worker, 1));
    thread_wait(thread_spawn(worker, 2));
    var s: i64 = 0;
    for j in TABLE.len { s += TABLE[j]; }
    print("main: ", s, " ", TEXT[259]);
}
