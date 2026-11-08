// src_file() is the base name of the file that contains the call.
// src_line() is that call's 1-based line. A default is parsed in the
// function that declares it; a call that leaves the argument out is
// stamped with that call. An explicit argument keeps what the caller
// wrote. A call inside the callee names the callee's own line.

fn note(text: u8[:], file: u8[:] = src_file(), row: i64 = src_line()) {
    print(file, ":", row, " ", text);
}

fn body_site() {
    print(src_file(), ":", src_line());
}

fn main() {
    print(src_file());
    print(src_line());
    note("filled");
    note("filled", "other.goose", 3);
    body_site();
}
