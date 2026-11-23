// Address, port and handle validation, including IPv6 loopback.
import std;
import tcp from "..";

// A module using gslib's namespace and listen signature can coexist.
fn net::listen(host: const u8[:], port: i64) -> i64 {
    if host.len == 0 { return -1; }
    return port;
}

fn show_connect(host: const u8[:], port: i64) {
    let s, ok = tcp::connect(host, port);
    print(s, " ", ok);
}
fn show_listen(host: const u8[:], port: i64) {
    let s, ok = tcp::listen(host, port);
    print(s, " ", ok);
}

fn main() {
    assert(net::listen("localhost", 1234) == 1234);
    // connect wants port 1..65535 and a host of 1..255 bytes with no NUL.
    show_connect("127.0.0.1", 0);                             // 0 false
    show_connect("127.0.0.1", 65536);                         // 0 false
    show_connect("127.0.0.1", -1);                            // 0 false
    show_connect("", 80);                                     // 0 false
    var nul: u8[>..] = [];
    nul.push(104);
    nul.push(0);
    nul.push(105);
    show_connect(nul, 80);                                    // 0 false
    var long_host: u8[>..] = [];
    for i in 0..300 { long_host.push(97); }
    show_connect(long_host, 80);                              // 0 false
    // listen wants port 0..65535; 0 picks an ephemeral port.
    show_listen("127.0.0.1", -1);                             // 0 false
    show_listen("127.0.0.1", 65536);                          // 0 false
    let l, ok = tcp::listen("127.0.0.1", 0);
    print(ok);                                                // true
    let p, pok = tcp::local_port(l);
    print(pok, " ", p > 0);                                   // true true
    print(tcp::send(l, ""));                                  // true
    print(tcp::close(l));                                     // true
    // A handle that is not a socket: every call fails, recv appends nothing.
    let bad = 999999;
    print(tcp::send(bad, "x"));                               // false
    var buf: u8[>..] = [];
    print(tcp::recv(bad, buf), " ", buf.len);                 // -1 0
    let a, aok = tcp::accept(bad);
    print(a, " ", aok);                                       // 0 false
    let bp, bok = tcp::local_port(bad);
    print(bp, " ", bok);                                      // 0 false
    print(tcp::close(bad));                                   // false
    // A handle past the int range too: lookup must refuse it, not read
    // its low bits as a slot index.
    let huge = 4294967296;
    print(tcp::send(huge, "x"));                             // false
    print(tcp::recv(huge, buf), " ", buf.len);               // -1 0
    print(tcp::close(huge));                                 // false
    print(tcp::close(-huge));                                // false

    // Zero is reserved for failure. A closed handle stays invalid when
    // the package reuses its slot for a new listener.
    print(tcp::close(0));                                    // false
    let next, nok = tcp::listen("127.0.0.1", 0);
    assert(nok && next != l);
    print(tcp::close(l));                                    // false
    let next_port, npok = tcp::local_port(next);
    print(npok && next_port > 0 && tcp::close(next));         // true

    let v6, v6ok = tcp::listen("::1", 0);
    if v6ok {
        let v6port, v6pok = tcp::local_port(v6);
        assert(v6pok);
        let v6client, connected = tcp::connect("::1", v6port);
        assert(connected);
        assert(tcp::close(v6client));
        assert(tcp::close(v6));
    }
}
