// stdlib/os.goose: a temp file written, appended, read back and deleted;
// arguments, environment, clocks.
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
    sleep(0.001);
    let seed = random_seed();
    print(seed == seed);                                        // true
    write_stdout("out\n");                                      // out
    flush_stdout();
}
