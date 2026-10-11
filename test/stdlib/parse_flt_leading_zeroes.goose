// runtime-debug: leading zeroes do not consume significant mantissa digits.
import std;

fn expect_value(text: u8[:], expected: f64) {
    let value,
    ok = parse_flt(text);
    assert(ok && value == expected);
    if expected == 0.0 {
        assert((1.0 / value < 0.0) == (1.0 / expected < 0.0));
    }
}

fn main() {
    expect_value("00000000000000000001", 1.0);
    expect_value("-00000000000000000001", -1.0);
    expect_value("+0000000000000000000123.5", 123.5);
    expect_value("00000000000000000001e2", 100.0);
    expect_value("00000000000000000000000000000000", 0.0);
    expect_value("-00000000000000000000000000000000.000", -0.0);

    expect_value("0.00000000000000000001", 1e-20);
    expect_value(".00000000000000000001", 1e-20);
    expect_value("-0.00000000000000000001", -1e-20);
    expect_value("00000000000000000000.00000000000000000001e20", 1.0);
    expect_value("0.00000000000000000000125e22", 12.5);

    // Leading zeroes retain room for significant digits and their trailing zeroes.
    expect_value("000000000000000000001234567890123456789000", 1234567890123456789000.0);
    expect_value("0.000000000000000000001234567890123456789e21", 1.2345678901234567);

    for text in["00000000000000000001x", "0.00000000000000000001e", "0.00000000000000000001e+"] {
        let value,
        ok = parse_flt(text);
        assert(!ok && value == 0.0);
    }
    print("leading zeroes parsed correctly");
}
