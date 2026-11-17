// runtime-debug: stdlib/os.goose: a temp file written, appended, read back
// and deleted; arguments, environment, clocks, entropy.
import os;

fn main() {
    let path = "gs_os_test_tmp.txt";
    print(write_file(path, "hello\nworld"));                    // true
    var buf: u8[>..] = [];
    print(read_file(path, buf), " ", buf.len, " ", buf);        // true 11 hello / world
    print(file_exists(path), " ", append_file(path, "!"));      // true true
    var b2: u8[>..] = [];
    read_file(path, b2);
    print(b2.len);                                              // 12
    print(delete_file(path), " ", file_exists(path));           // true false
    print(read_file("no_such_file_here.txt", b2));              // false
    let a = args();
    print(a.len >= 1);                                          // true
    let t0 = clock();
    let t1 = clock();
    print(t1 >= t0);                                            // true
    print(time() > 1600000000.0);                               // true
    var e: u8[>..] = [];
    print(env("GOOSE_NO_SUCH_VAR_", e), " ", e.len);            // false 0
    sleep(0.0015);                                              // 1 ms: the fraction is dropped
    let seed = random_seed();
    print(seed == seed);                                        // true
    // 1000 bytes take several of getentropy's 256-byte calls: each part of
    // them is filled, and a second draw differs.
    var r1: u8[1000] = [0; 1000];
    var r2: u8[1000] = [0; 1000];
    print(random_bytes(r1), " ", random_bytes(r2));             // true true
    var filled = true;
    each_chunk(r1, 250) { c, i =>
        var zero = true;
        for x in c { if x != 0 { zero = false; } }
        if zero { filled = false; }
    };
    print(filled, " ", r1 != r2);                               // true true
    var none: u8[:] = r1[0..0];
    print(random_bytes(none));                                  // true
    write_stdout("out\n");                                      // out
    flush_stdout();
}
