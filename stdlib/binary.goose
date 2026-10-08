// Checked reads of external binary formats, independent of from_bytes' image
// format. Slices and names borrow the input; no read allocates or copies bytes.
namespace binary;
import std;

// Negative offsets/sizes wrap above every valid slice length. Check the
// offset before subtracting, so even extreme inputs cannot wrap the range.
fn range_ok(data: const u8[:], at: i64, size: i64) -> bool {
    let start = at as! u64;
    start <= data.len as u64 && (size as! u64) <= data.len as u64 - start
}

// Offset reads return zero/an empty slice and false on an invalid range.
fn read_bytes(data: const u8[:], at: i64, size: i64) -> const u8[:], bool {
    if !range_ok(data, at, size) { return data[..0], false; }
    return data[at..at + size], true;
}
fn read_u16le(data: const u8[:], at: i64) -> u16, bool {
    if !range_ok(data, at, 2) { return 0, false; }
    return data[at] | (data[at + 1] as u16 << 8), true;
}
fn read_i16le(data: const u8[:], at: i64) -> i16, bool {
    let value, ok = read_u16le(data, at);
    return value as! i16, ok;
}
fn read_u32le(data: const u8[:], at: i64) -> u32, bool {
    if !range_ok(data, at, 4) { return 0, false; }
    return data[at] | (data[at + 1] as u32 << 8) |
           (data[at + 2] as u32 << 16) | (data[at + 3] as u32 << 24), true;
}

// Validate the entire field, then trim at its first NUL (or keep the full
// width if none). These are bytes: no encoding or padding validation is done.
fn read_name(data: const u8[:], at: i64, width: i64) -> const u8[:], bool {
    let field, ok = read_bytes(data, at, width);
    if !ok { return field, false; }
    var size = 0;
    while size < field.len && field[size] != 0 { size++; }
    return field[..size], true;
}

// Every read returns its own success flag. Failures leave pos unchanged, so
// callers may try another read. A name consumes its entire field width.
struct Reader { data: const u8[:], pos: i64 }
fn reader(data: const u8[:]) -> Reader { Reader { data, 0 } }

fn read_bytes(r: Reader&, size: i64) -> const u8[:], bool {
    let data, ok = read_bytes(r.data, r.pos, size);
    if ok { r.pos += size; }
    return data, ok;
}
fn read_u16le(r: Reader&) -> u16, bool {
    let value, ok = read_u16le(r.data, r.pos);
    if ok { r.pos += 2; }
    return value, ok;
}
fn read_i16le(r: Reader&) -> i16, bool {
    let value, ok = read_u16le(r);
    return value as! i16, ok;
}
fn read_u32le(r: Reader&) -> u32, bool {
    let value, ok = read_u32le(r.data, r.pos);
    if ok { r.pos += 4; }
    return value, ok;
}
fn read_name(r: Reader&, width: i64) -> const u8[:], bool {
    let name, ok = read_name(r.data, r.pos, width);
    if ok { r.pos += width; }
    return name, ok;
}
fn skip(r: Reader&, size: i64) -> bool {
    if !range_ok(r.data, r.pos, size) { return false; }
    r.pos += size;
    return true;
}

// Consumes size bytes from the parent; the child's position starts at zero.
// On failure, returns an empty reader and leaves the parent in place.
fn subreader(r: Reader&, size: i64) -> Reader, bool {
    let data, ok = read_bytes(r, size);
    return reader(data), ok;
}
