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

    // Returned data borrows the original storage, including through a child.
    var source: u8[4] = [65, 66, 67, 68];
    var borrowed = binary::reader(source);
    var borrowed_child, borrowedok = binary::subreader(borrowed, 4);
    let view, viewok = binary::read_name(borrowed_child, 4);
    source[0] = 90;
    assert(borrowedok && viewok && view == "ZBCD");
    print("binary: checked offsets, names, cursors and subreaders passed");
}
