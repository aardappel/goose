// The digit loops std parse_flt is built from keep no bounds checks: one
// that binds the byte and breaks on a non-digit, and a second index reading
// the same digits again. That one is a new variable: assigning the first
// index anew would lose its facts in the loops before the assignment too.
import std;

fn scan(s: u8[:]) -> u64 {
    var i = 0;
    if s.len > 0 && s[0] == '-' { i = 1; }            // bce:elide
    let start = i;
    var mant: u64 = 0;
    while i < s.len {
        let d = s[i] - '0';                            // bce:elide
        if d >= 10 { break; }
        mant = mant * 10 + d;
        i += 1;
    }
    if i < s.len && s[i] == '.' {                      // bce:elide
        i += 1;
        while i < s.len {
            let d = s[i] - '0';                        // bce:elide
            if d >= 10 { break; }
            mant = mant * 10 + d;
            i += 1;
        }
    }
    if i - start > 3 {
        var again: u64 = 0;
        var j = start;
        while j < s.len && s[j] - '0' < 10 {           // bce:elide
            again = again * 10 + (s[j] - '0');         // bce:elide
            j += 1;
        }
        mant += again;
    }
    return mant;
}

fn main() {
    print(scan("12.5"), " ", scan("-12345.678"), " ", scan("x"), " ", scan(""));
}
