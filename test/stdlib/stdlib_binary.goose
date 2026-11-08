// runtime-debug: external bytes, hostile ranges, borrowed slices and cursor failures.
import binary;

fn main() {
    let data: u8[12] = [255, 255, 0, 128, 255, 255, 255, 255, 65, 0, 88, 89];
    let u, uok = binary::read_u16le(data, 0);
    let neg, nok = binary::read_i16le(data, 0);
    let low, lok = binary::read_i16le(data, 2);
    let big, bok = binary::read_u32le(data, 4);
    assert(uok && u == 65535 && nok && neg == -1 && lok && low == -32768);
    assert(bok && big == 4294967295);
    let unaligned, aok = binary::read_u32le(data, 1);
    assert(aok && unaligned == 4286578943);
    let positive, pok = binary::read_i16le([255, 127], 0);
    assert(pok && positive == 32767);
    let zero, zok = binary::read_i16le([0, 0], 0);
    assert(zok && zero == 0);
    for at in [-1, 11, 12, 9223372036854775807, -9223372036854775807 - 1] {
        let v, ok = binary::read_u16le(data, at);
        assert(!ok && v == 0);
    }
    for size in [-1, 13, 9223372036854775807] {
        let bytes, ok = binary::read_bytes(data, 1, size);
        assert(!ok && bytes.len == 0);
    }
    let end, eok = binary::read_bytes(data, data.len, 0);
    assert(eok && end.len == 0);
    let badend, beok = binary::read_bytes(data, data.len + 1, 0);
    assert(!beok && badend.len == 0);
    let short, sok = binary::read_u32le(data, 9);
    assert(!sok && short == 0);
    let short_signed, ssok = binary::read_i16le(data, 11);
    assert(!ssok && short_signed == 0);

    let name, name_ok = binary::read_name(data, 8, 4);
    assert(name_ok && name == "A");
    let full, full_ok = binary::read_name(data, 10, 2);
    assert(full_ok && full == "XY");
    let empty, empty_ok = binary::read_name(data, 9, 1);
    assert(empty_ok && empty.len == 0);
    let no_field, no_field_ok = binary::read_name(data, 8, 5);
    assert(!no_field_ok && no_field.len == 0); // Early NUL cannot hide truncation.
    let empty_field, efok = binary::read_name(data, data.len, 0);
    assert(efok && empty_field.len == 0);

    var r = binary::reader(data);
    let ru, ruok = binary::read_u16le(r);
    let ri, riok = binary::read_i16le(r);
    let rb, rbok = binary::read_u32le(r);
    let rn, rnok = binary::read_name(r, 4);
    assert(ruok && ru == 65535 && riok && ri == -32768);
    assert(rbok && rb == 4294967295 && rnok && rn == "A" && r.pos == 12);
    let endbytes, endok = binary::read_bytes(r, 0);
    assert(endok && endbytes.len == 0);
    let past, pastok = binary::read_u16le(r);
    assert(!pastok && past == 0 && r.pos == 12);
    assert(binary::skip(r, 0)); // No sticky failure.

    var parent = binary::reader(data);
    assert(binary::skip(parent, 4));
    var child, childok = binary::subreader(parent, 6);
    assert(childok && parent.pos == 10 && child.pos == 0);
    assert(binary::skip(child, 4));
    var nested, nestedok = binary::subreader(child, 2);
    let nestedname, nestednameok = binary::read_name(nested, 2);
    assert(nestedok && nestednameok && nestedname == "A");
    let outside, outsideok = binary::read_u32le(nested);
    assert(!outsideok && outside == 0 && nested.pos == 2);
    let parentname, parentnameok = binary::read_name(parent, 2);
    assert(parentnameok && parentname == "XY");
    var failed, failedok = binary::subreader(parent, 1);
    assert(!failedok && parent.pos == 12 && failed.data.len == 0 && failed.pos == 0);

    var truncated = binary::reader(data[..2]);
    let large, largeok = binary::read_u32le(truncated);
    assert(!largeok && large == 0 && truncated.pos == 0);
    let retry, retryok = binary::read_i16le(truncated);
    assert(retryok && retry == -1 && truncated.pos == 2);
    var invalid = binary::reader(data);
    assert(!binary::skip(invalid, -1) && invalid.pos == 0);
    let huge, hugeok = binary::read_bytes(invalid, 9223372036854775807);
    assert(!hugeok && huge.len == 0 && invalid.pos == 0);
    invalid.pos = -1;
    let before, beforeok = binary::read_i16le(invalid);
    assert(!beforeok && before == 0 && invalid.pos == -1);
    invalid.pos = 9223372036854775807;
    assert(!binary::skip(invalid, 0));
    var none = binary::reader(data[..0]);
    var empty_child, empty_childok = binary::subreader(none, 0);
    assert(empty_childok && empty_child.data.len == 0 && empty_child.pos == 0);
    var names = binary::reader(data[8..]);
    let badname, badnameok = binary::read_name(names, 5);
    assert(!badnameok && badname.len == 0 && names.pos == 0);
    let goodname, goodnameok = binary::read_name(names, 4);
    assert(goodnameok && goodname == "A" && names.pos == 4);

    // Every width, both byte orders. wide holds 0x0102030405060708, then
    // 1.5f32 and 1.5f64 little-endian, then the same two big-endian.
    let wide: u8[32] = [1, 2, 3, 4, 5, 6, 7, 8,
                        0, 0, 192, 63, 0, 0, 0, 0, 0, 0, 248, 63,
                        63, 192, 0, 0, 63, 248, 0, 0, 0, 0, 0, 0];
    let b8, b8ok = binary::read_u8(data, 3);
    let s8, s8ok = binary::read_i8(data, 3);
    let p8, p8ok = binary::read_i8(wide, 0);
    assert(b8ok && b8 == 128 && s8ok && s8 == -128 && p8ok && p8 == 1);
    let u16be, u16beok = binary::read_u16be(wide, 0);
    let u32be, u32beok = binary::read_u32be(wide, 0);
    let u64be, u64beok = binary::read_u64be(wide, 0);
    let u64le, u64leok = binary::read_u64le(wide, 0);
    assert(u16beok && u16be == 0x0102 && u32beok && u32be == 0x01020304);
    assert(u64beok && u64be == 0x0102030405060708 && u64leok && u64le == 0x0807060504030201);
    let i16be, i16beok = binary::read_i16be(data, 2);
    let i32le, i32leok = binary::read_i32le(data, 4);
    let i32be, i32beok = binary::read_i32be(data, 0);
    let i64le, i64leok = binary::read_i64le(data, 0);
    let i64be, i64beok = binary::read_i64be(data, 0);
    assert(i16beok && i16be == 128 && i32leok && i32le == -1 && i32beok && i32be == -65408);
    assert(i64leok && i64le == -2147418113 && i64beok && i64be == -280920925929473);
    let f32le, f32leok = binary::read_f32le(wide, 8);
    let f64le, f64leok = binary::read_f64le(wide, 12);
    let f32be, f32beok = binary::read_f32be(wide, 20);
    let f64be, f64beok = binary::read_f64be(wide, 24);
    assert(f32leok && f32le == 1.5 && f64leok && f64le == 1.5);
    assert(f32beok && f32be == 1.5 && f64beok && f64be == 1.5);
    let nan32, nan32ok = binary::read_f32le(data, 4);
    let nan64, nan64ok = binary::read_f64be(data, 0);
    assert(nan32ok && nan32 != nan32 && nan64ok && nan64 != nan64);
    let nan_bits: f64[1] = [nan64]; // The payload survives the read.
    assert(bytes_of(nan_bits)[7] == 255 && bytes_of(nan_bits)[0] == 255);
    let short64, short64ok = binary::read_u64le(data, 5);
    let shortf, shortfok = binary::read_f64be(data, 5);
    let short8, short8ok = binary::read_u8(data, data.len);
    let neg32, neg32ok = binary::read_i32be(data, -1);
    assert(!short64ok && short64 == 0 && !shortfok && shortf == 0.0);
    assert(!short8ok && short8 == 0 && !neg32ok && neg32 == 0);
    let last8, last8ok = binary::read_i8(data, 11);
    assert(last8ok && last8 == 89);

    var w = binary::reader(wide);
    let wu8, wu8ok = binary::read_u8(w);
    let wi8, wi8ok = binary::read_i8(w);
    let wu16, wu16ok = binary::read_u16be(w);
    let wi32, wi32ok = binary::read_i32be(w);
    let wf32, wf32ok = binary::read_f32le(w);
    let wf64, wf64ok = binary::read_f64le(w);
    assert(wu8ok && wu8 == 1 && wi8ok && wi8 == 2 && wu16ok && wu16 == 0x0304);
    assert(wi32ok && wi32 == 0x05060708 && wf32ok && wf32 == 1.5 && wf64ok && wf64 == 1.5);
    let wbf32, wbf32ok = binary::read_f32be(w);
    let wbf64, wbf64ok = binary::read_f64be(w);
    assert(wbf32ok && wbf32 == 1.5 && wbf64ok && wbf64 == 1.5 && w.pos == 32);
    let wtoo_far, wtoo_farok = binary::read_u8(w);
    assert(!wtoo_farok && wtoo_far == 0 && w.pos == 32);
    var e = binary::reader(wide);
    let eu32, eu32ok = binary::read_u32le(e);
    let ei16, ei16ok = binary::read_i16le(e);
    let eu16, eu16ok = binary::read_u16le(e);
    let eu64, eu64ok = binary::read_u64le(e);
    let ei64, ei64ok = binary::read_i64be(e);
    let eu32b, eu32bok = binary::read_u32be(e);
    let ei16b, ei16bok = binary::read_i16be(e);
    let ei64l, ei64lok = binary::read_i64le(e);
    assert(eu32ok && eu32 == 0x04030201 && ei16ok && ei16 == 0x0605 && eu16ok && eu16 == 0x0807);
    assert(eu64ok && eu64 == 0x3FC00000 && ei64ok && ei64 == 0xF83F3FC00000);
    assert(eu32bok && eu32b == 0x3FF80000 && ei16bok && ei16b == 0);
    assert(!ei64lok && ei64l == 0 && e.pos == 30);

    // Writes append the bytes the reads take. out starts with a byte of its
    // own, which every write leaves in place.
    var out: u8[>..] = [9];
    binary::write_u64be(out, 0x0102030405060708);
    binary::write_f32le(out, 1.5);
    binary::write_f64le(out, 1.5);
    binary::write_f32be(out, 1.5);
    binary::write_f64be(out, 1.5);
    assert(out.len == 33 && out[0] == 9 && out[1..] == wide[..]);
    out.clear();
    binary::write_u8(out, 255);
    binary::write_i8(out, -128);
    binary::write_u16le(out, 0x0102);
    binary::write_u16be(out, 0x0102);
    binary::write_i16le(out, -2);
    binary::write_i16be(out, -32768);
    binary::write_u32le(out, 0x01020304);
    binary::write_i32be(out, -2);
    binary::write_u64le(out, 0x0102030405060708);
    binary::write_i64le(out, -9223372036854775807 - 1);
    binary::write_i64be(out, -2);
    let expect: u8[42] = [255, 128, 2, 1, 1, 2, 254, 255, 128, 0,
                          4, 3, 2, 1, 255, 255, 255, 254,
                          8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 128,
                          255, 255, 255, 255, 255, 255, 255, 254];
    assert(out.len == 42 && out[..] == expect[..]);

    // Every extreme of every width reads back as it was written.
    out.clear();
    binary::write_i8(out, 127);
    binary::write_i16le(out, 32767);
    binary::write_i16be(out, -1);
    binary::write_u16be(out, 65535);
    binary::write_i32le(out, -2147483647 - 1);
    binary::write_u32be(out, 4294967295);
    binary::write_i32be(out, 2147483647);
    binary::write_u64be(out, 18446744073709551615);
    binary::write_i64le(out, 9223372036854775807);
    binary::write_f64be(out, -0.0);
    var x = binary::reader(out);
    let xi8, xi8ok = binary::read_i8(x);
    let xi16, xi16ok = binary::read_i16le(x);
    let xi16b, xi16bok = binary::read_i16be(x);
    let xu16b, xu16bok = binary::read_u16be(x);
    let xi32, xi32ok = binary::read_i32le(x);
    let xu32b, xu32bok = binary::read_u32be(x);
    let xi32b, xi32bok = binary::read_i32be(x);
    let xu64b, xu64bok = binary::read_u64be(x);
    let xi64, xi64ok = binary::read_i64le(x);
    let xz, xzok = binary::read_u64be(x);
    assert(xi8ok && xi8 == 127 && xi16ok && xi16 == 32767 && xi16bok && xi16b == -1);
    assert(xu16bok && xu16b == 65535 && xi32ok && xi32 == -2147483647 - 1);
    assert(xu32bok && xu32b == 4294967295 && xi32bok && xi32b == 2147483647);
    assert(xu64bok && xu64b == 18446744073709551615 && xi64ok && xi64 == 9223372036854775807);
    assert(xzok && xz == 0x8000000000000000 && x.pos == out.len);

    // A float keeps its bits through a read and a write: signaling and quiet
    // NaNs with payloads, in either sign, and negative zero.
    let nans: u8[24] = [1, 0, 160, 127, 69, 35, 193, 255,
                        1, 0, 0, 0, 0, 0, 240, 127,
                        255, 255, 255, 255, 255, 255, 248, 255];
    out.clear();
    for at in [0, 4] {
        let v, ok = binary::read_f32le(nans, at);
        assert(ok && v != v);
        binary::write_f32le(out, v);
        binary::write_f32be(out, v);
    }
    for at in [8, 16] {
        let v, ok = binary::read_f64le(nans, at);
        assert(ok && v != v);
        binary::write_f64le(out, v);
        binary::write_f64be(out, v);
    }
    assert(out.len == 48 && out[0..4] == nans[0..4] && out[8..12] == nans[4..8]);
    assert(out[16..24] == nans[8..16] && out[32..40] == nans[16..]);
    // The be copies hold the same bits, most significant byte first.
    let sn32, _ = binary::read_u32le(nans, 0);
    let qn32, _ = binary::read_u32le(nans, 4);
    let sn64, _ = binary::read_u64le(nans, 8);
    let qn64, _ = binary::read_u64le(nans, 16);
    let sn32b, _ = binary::read_u32be(out, 4);
    let qn32b, _ = binary::read_u32be(out, 12);
    let sn64b, _ = binary::read_u64be(out, 24);
    let qn64b, _ = binary::read_u64be(out, 40);
    assert(sn32b == sn32 && qn32b == qn32 && sn64b == sn64 && qn64b == qn64);
    let zero32: u8[4] = [0, 0, 0, 128];
    let negzero, negzerook = binary::read_f32le(zero32, 0);
    let zero32be: u8[4] = [128, 0, 0, 0];
    out.clear();
    binary::write_f32be(out, negzero);
    assert(negzerook && negzero == 0.0 && out[..] == zero32be[..]);

    // Returned data borrows the original storage, including through a child.
    var source: u8[4] = [65, 66, 67, 68];
    var borrowed = binary::reader(source);
    var borrowed_child, borrowedok = binary::subreader(borrowed, 4);
    let view, viewok = binary::read_name(borrowed_child, 4);
    source[0] = 90;
    assert(borrowedok && viewok && view == "ZBCD");
    print("binary: checked offsets, widths, byte orders, names, cursors, subreaders and writes passed");
}
