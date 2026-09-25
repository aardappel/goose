// Only the documented OS ABI needed by the compiler. Keeping this boundary
// small lets the compiler bootstrap without depending on the rest of std.
extern "gs_os_read_file" fn read_file(path: const u8[:], out: u8[>..]&) -> bool;
extern "gs_os_write_file" fn write_file(path: const u8[:], data: const u8[:]) -> bool;
extern "gs_os_file_exists" fn file_exists(path: const u8[:]) -> bool;
extern "gs_os_write_stderr" fn write_stderr(text: const u8[:]);
extern "gs_os_arg_count" fn argument_count() -> i64;
extern "gs_os_arg" fn argument_text(index: i64, out: u8[>..]&);

fn args(text_storage: u8[>..]&) -> (const u8[:])[>..] {
    var result: (const u8[:])[>..] = [];
    var index = 0;
    while index < argument_count() {
        let begin = text_storage.len;
        argument_text(index, text_storage);
        result.push(text_storage[begin..]);
        index++;
    }
    return result;
}
