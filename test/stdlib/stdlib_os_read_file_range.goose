// runtime-debug: bounded file reads, including binary data and short reads.
import os;

fn main() {
    let path = "gs_os_range_test_é.bin";
    assert(write_file(path, "A\0BCDE"));
    var out: u8[>..] = [];
    out.append("prefix");
    print(read_file(path, 1, 3, out), " ", out == "prefix\0BC");
    print(read_file(path, 4, 10, out), " ", out == "prefix\0BCDE");
    print(read_file(path, 6, 10, out), " ", read_file(path, 30, 10, out));
    print(read_file(path, 0, 0, out), " ", read_file(path, -1, 2, out), " ", read_file(path, 0, -1, out));
    assert(delete_file(path));
    print(read_file(path, 0, 2, out), " ", out == "prefix\0BCDE");

    var data: u8[>..] = [];
    data.resize(65539, 'x');
    data.append("end");
    assert(write_file(path, data));
    out.clear();
    print(read_file(path, 2, 65538, out), " ", out == data[2..65540]);
    out.clear();
    print(read_file(path, 65537, 9223372036854775807, out), " ", out == "xxend");
    assert(delete_file(path));
}
