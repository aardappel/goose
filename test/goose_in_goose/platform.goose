// Only the documented OS ABI needed by the compiler. Keeping this boundary
// small lets the compiler bootstrap without depending on the rest of std.
extern fn read_file(path: const u8[:], out: u8[>..]&) -> bool = "gs_os_read_file";
extern fn write_file(path: const u8[:], data: const u8[:]) -> bool = "gs_os_write_file";
extern fn file_exists(path: const u8[:]) -> bool = "gs_os_file_exists";
extern fn write_stderr(text: const u8[:]) = "gs_os_write_stderr";
extern fn argument_count() -> i64 = "gs_os_arg_count";
extern fn argument_text(index: i64, out: u8[>..]&) = "gs_os_arg";

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
