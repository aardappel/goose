// runtime-debug: a `for` loads the slices and references on the path to its
// sequence again on every iteration (§6.5), so a body that re-points one
// moves the walk to the elements the path leads to now, at the next index,
// as the length read again does: a walk over variable-size elements too,
// whose cursor walks there from their start, and one over the elements
// behind a varint length prefix.
var wbig: u8[][>..] = [];
var wsmall: u8[][>..] = [];
var wpair: u8[][>..] = [];
var ws: u8[][:] = wsmall[..];
struct HS { s: u8[][:] }
var hs = HS { s: wsmall[..] };
var rw: u8[][>..]& = wpair;

var a1: i64[varint] = [1];
var a2: i64[varint] = [1, 2, 3];
var ra: i64[varint]& = a1;

var fbig: i64[>..] = [];
var fsmall: i64[>..] = [];
var fs: i64[:] = fsmall[..];

fn repoint() { ws = wbig[..]; }

fn main() {
    wbig.push("aaa");
    wbig.push("bbb");
    wbig.push("ccc");
    wsmall.push("x");
    wpair.push("p");
    wpair.push("q");
    ws = wsmall[..];
    for w in ws {
        print(w);                               // x bbb ccc
        ws = wbig[..];
    }
    hs.s = wsmall[..];
    for w in hs.s {
        print(w);                               // x bbb ccc
        hs.s = wbig[..];
    }
    for w, i in rw {
        print(w);                               // p q ccc
        if i == 1 { rw .= wbig; }
    }
    ws = wsmall[..];
    for w in ws {
        print(w);                               // x bbb ccc
        repoint();
    }

    // The same elements, grown meanwhile: the walk goes on past the ones it
    // began with.
    ws = wbig[..];
    for w, i in ws {
        if i == 0 {
            wbig.push("ddd");
            ws = wbig[..];
            continue;
        }
        print(i, " ", w);                       // 1 bbb, 2 ccc, 3 ddd
    }

    for x in ra {
        print(x);                               // 1 2 3
        ra .= a2;
    }

    // Fixed-size elements are read by index, as ever.
    for i in 1..4 { fbig.push(i); }
    fsmall.push(9);
    fs = fsmall[..];
    for x in fs {
        print(x);                               // 9 2 3
        fs = fbig[..];
    }
}
