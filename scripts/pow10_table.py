"""Prints float_text::POW10_128, the table at the end of stdlib/std.goose that
its Eisel-Lemire reading multiplies by: for each q from -348 to 347, the
leading 128 bits of 10^q, rounded down, as two u64s, the high half first.
These are the values of Go's strconv detailedPowersOfTen (its comment gives
1e43 as 0xE596B7B0_C643C719_6D9CCD05_D0000000), which the asserts check.

    python scripts/pow10_table.py
"""

LO, HI = -348, 347


def leading_128(q):
    if q >= 0:
        v = 10 ** q
        b = v.bit_length()
        return v >> (b - 128) if b > 128 else v << (128 - b)
    # floor(2^k / 10^-q) for the k that makes it exactly 128 bits long
    d = 10 ** -q
    k = d.bit_length() + 127
    m = (1 << k) // d
    if m.bit_length() < 128:
        m = (1 << (k + 1)) // d
    assert m.bit_length() == 128
    return m


def main():
    assert leading_128(43) == 0xE596B7B0C643C7196D9CCD05D0000000
    assert leading_128(-348) == 0xFA8FD5A0081C02881732C869CD60E453
    assert leading_128(-1) == 0xCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
    print(f"const float_text::POW10_128: u64[{2 * (HI - LO + 1)}] = [")
    mask = (1 << 64) - 1
    for q in range(LO, HI + 1, 2):
        cells = " ".join(f"0x{leading_128(r) >> 64:016X}, 0x{leading_128(r) & mask:016X}," for r in (q, q + 1))
        print(f"    {cells}   // 1e{q}, 1e{q + 1}")
    print("];")


if __name__ == "__main__":
    main()
