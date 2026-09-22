struct V { x: varint }
struct B { flags: bool[2] }
fn main() {
    let badvar: u8[3] = [2, 128, 0];
    let vs, vok = from_bytes<V[>..]>(badvar);
    assert(!vok && vs.len == 0);
    let badbool: u8[2] = [1, 2];
    let bs, bok = from_bytes<bool[>..]>(badbool);
    assert(!bok && bs.len == 0);
    let nested: u8[3] = [2, 0, 255];
    let ns, nok = from_bytes<B[>..]>(nested);
    assert(!nok && ns.len == 0);
    let badprefix: u8[2] = [128, 0];
    let ps, pok = from_bytes<u8[>..]>(badprefix);
    assert(!pok && ps.len == 0);
    let values: V[>..] = [V { 0 }, V { -1 }, V { 64 }, V { -9223372036854775808 }];
    let image = to_bytes(values);
    let copy, ok = from_bytes<V[>..]>(image);
    assert(ok && values == copy);
    let goodbool: u8[3] = [2, 0, 1];
    let good, goodok = from_bytes<bool[>..]>(goodbool);
    assert(goodok && !good[0] && good[1]);
    print("canonical images verified");
}
