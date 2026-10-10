// A shrink followed in its block by a `return` or a `break` that nothing
// before it can skip with a `continue` runs nothing after it again: neither
// the rest of the function or of the loop, nor the loop's next iteration.
// So a holder of views into the array that only those name is not live at
// the shrink (§5.1): the decoder's result is cleared on an invalid code
// while its dictionary of views into it is still in scope.
fn decode(data: u8[:]) -> u8[>..] {
    var result: u8[>..] = [];
    var dict: (const u8[:])[>..] = [];
    var pos = 0;
    while pos < data.len {
        let start = result.len;
        if data[pos] == 0 {
            result.clear();
            return result;
        }
        result.push(data[pos]);
        dict.push(result[start..start + 1]);
        pos += 1;
    }
    return result;
}

fn decode_break(data: u8[:]) -> u8[>..] {
    var result: u8[>..] = [];
    var dict: (const u8[:])[>..] = [];
    var pos = 0;
    while pos < data.len {
        let start = result.len;
        if data[pos] == 0 {
            result.clear();
            break
        }
        result.push(data[pos]);
        dict.push(result[start..start + 1]);
        pos += 1;
    }
    return result;
}

fn first_or_empty(data: u8[:]) -> u8[>..] {
    var result: u8[>..] = [];
    var dict: (const u8[:])[>..] = [];
    result.push(1);
    dict.push(result[0..1]);
    if data.len == 0 {
        result.clear();
        return result;
    }
    dict.push(result[0..1]);
    print(dict.len);
    return result;
}

fn main() {
    let a: u8[3] = [65, 66, 67];
    let b: u8[3] = [65, 0, 67];
    print(decode(a), " ", decode(b).len, " ", decode_break(a), " ", decode_break(b).len);
    print(first_or_empty(a).len, " ", first_or_empty(a[0..0]).len);
}
