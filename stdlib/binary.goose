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
// Little- and big-endian values at an offset the caller has checked.
fn u16le(data: const u8[:], at: i64) -> u16 { data[at] | (data[at + 1] as u16 << 8) }
fn u32le(data: const u8[:], at: i64) -> u32 { u16le(data, at) | (u16le(data, at + 2) as u32 << 16) }
fn u64le(data: const u8[:], at: i64) -> u64 { u32le(data, at) | (u32le(data, at + 4) as u64 << 32) }
fn u16be(data: const u8[:], at: i64) -> u16 { (data[at] as u16 << 8) | data[at + 1] }
fn u32be(data: const u8[:], at: i64) -> u32 { (u16be(data, at) as u32 << 16) | u16be(data, at + 2) }
fn u64be(data: const u8[:], at: i64) -> u64 { (u32be(data, at) as u64 << 32) | u32be(data, at + 4) }

// Floats keep their bits exactly, NaN payloads included.
extern fn f32_of_bits(bits: u32) -> f32 = "gs_f32_from_bits";
extern fn f64_of_bits(bits: u64) -> f64 = "gs_f64_from_bits";

fn read_u8(data: const u8[:], at: i64) -> u8, bool {
    if !range_ok(data, at, 1) { return 0, false; }
    return data[at], true;
}
fn read_i8(data: const u8[:], at: i64) -> i8, bool {
    let value, ok = read_u8(data, at);
    return value as! i8, ok;
}
fn read_u16le(data: const u8[:], at: i64) -> u16, bool {
    if !range_ok(data, at, 2) { return 0, false; }
    return u16le(data, at), true;
}
fn read_i16le(data: const u8[:], at: i64) -> i16, bool {
    let value, ok = read_u16le(data, at);
    return value as! i16, ok;
}
fn read_u32le(data: const u8[:], at: i64) -> u32, bool {
    if !range_ok(data, at, 4) { return 0, false; }
    return u32le(data, at), true;
}
fn read_i32le(data: const u8[:], at: i64) -> i32, bool {
    let value, ok = read_u32le(data, at);
    return value as! i32, ok;
}
fn read_u64le(data: const u8[:], at: i64) -> u64, bool {
    if !range_ok(data, at, 8) { return 0, false; }
    return u64le(data, at), true;
}
fn read_i64le(data: const u8[:], at: i64) -> i64, bool {
    let value, ok = read_u64le(data, at);
    return value as! i64, ok;
}
fn read_f32le(data: const u8[:], at: i64) -> f32, bool {
    let bits, ok = read_u32le(data, at);
    return f32_of_bits(bits), ok;
}
fn read_f64le(data: const u8[:], at: i64) -> f64, bool {
    let bits, ok = read_u64le(data, at);
    return f64_of_bits(bits), ok;
}
fn read_u16be(data: const u8[:], at: i64) -> u16, bool {
    if !range_ok(data, at, 2) { return 0, false; }
    return u16be(data, at), true;
}
fn read_i16be(data: const u8[:], at: i64) -> i16, bool {
    let value, ok = read_u16be(data, at);
    return value as! i16, ok;
}
fn read_u32be(data: const u8[:], at: i64) -> u32, bool {
    if !range_ok(data, at, 4) { return 0, false; }
    return u32be(data, at), true;
}
fn read_i32be(data: const u8[:], at: i64) -> i32, bool {
    let value, ok = read_u32be(data, at);
    return value as! i32, ok;
}
fn read_u64be(data: const u8[:], at: i64) -> u64, bool {
    if !range_ok(data, at, 8) { return 0, false; }
    return u64be(data, at), true;
}
fn read_i64be(data: const u8[:], at: i64) -> i64, bool {
    let value, ok = read_u64be(data, at);
    return value as! i64, ok;
}
fn read_f32be(data: const u8[:], at: i64) -> f32, bool {
    let bits, ok = read_u32be(data, at);
    return f32_of_bits(bits), ok;
}
fn read_f64be(data: const u8[:], at: i64) -> f64, bool {
    let bits, ok = read_u64be(data, at);
    return f64_of_bits(bits), ok;
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
// Advances pos past a successful read of size bytes.
fn advance(r: Reader&, ok: bool, size: i64) -> bool {
    if ok { r.pos += size; }
    return ok;
}
fn read_u8(r: Reader&) -> u8, bool {
    let value, ok = read_u8(r.data, r.pos);
    return value, advance(r, ok, 1);
}
fn read_i8(r: Reader&) -> i8, bool {
    let value, ok = read_i8(r.data, r.pos);
    return value, advance(r, ok, 1);
}
fn read_u16le(r: Reader&) -> u16, bool {
    let value, ok = read_u16le(r.data, r.pos);
    return value, advance(r, ok, 2);
}
fn read_i16le(r: Reader&) -> i16, bool {
    let value, ok = read_i16le(r.data, r.pos);
    return value, advance(r, ok, 2);
}
fn read_u32le(r: Reader&) -> u32, bool {
    let value, ok = read_u32le(r.data, r.pos);
    return value, advance(r, ok, 4);
}
fn read_i32le(r: Reader&) -> i32, bool {
    let value, ok = read_i32le(r.data, r.pos);
    return value, advance(r, ok, 4);
}
fn read_u64le(r: Reader&) -> u64, bool {
    let value, ok = read_u64le(r.data, r.pos);
    return value, advance(r, ok, 8);
}
fn read_i64le(r: Reader&) -> i64, bool {
    let value, ok = read_i64le(r.data, r.pos);
    return value, advance(r, ok, 8);
}
fn read_f32le(r: Reader&) -> f32, bool {
    let value, ok = read_f32le(r.data, r.pos);
    return value, advance(r, ok, 4);
}
fn read_f64le(r: Reader&) -> f64, bool {
    let value, ok = read_f64le(r.data, r.pos);
    return value, advance(r, ok, 8);
}
fn read_u16be(r: Reader&) -> u16, bool {
    let value, ok = read_u16be(r.data, r.pos);
    return value, advance(r, ok, 2);
}
fn read_i16be(r: Reader&) -> i16, bool {
    let value, ok = read_i16be(r.data, r.pos);
    return value, advance(r, ok, 2);
}
fn read_u32be(r: Reader&) -> u32, bool {
    let value, ok = read_u32be(r.data, r.pos);
    return value, advance(r, ok, 4);
}
fn read_i32be(r: Reader&) -> i32, bool {
    let value, ok = read_i32be(r.data, r.pos);
    return value, advance(r, ok, 4);
}
fn read_u64be(r: Reader&) -> u64, bool {
    let value, ok = read_u64be(r.data, r.pos);
    return value, advance(r, ok, 8);
}
fn read_i64be(r: Reader&) -> i64, bool {
    let value, ok = read_i64be(r.data, r.pos);
    return value, advance(r, ok, 8);
}
fn read_f32be(r: Reader&) -> f32, bool {
    let value, ok = read_f32be(r.data, r.pos);
    return value, advance(r, ok, 4);
}
fn read_f64be(r: Reader&) -> f64, bool {
    let value, ok = read_f64be(r.data, r.pos);
    return value, advance(r, ok, 8);
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
