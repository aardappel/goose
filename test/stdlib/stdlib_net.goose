// runtime-debug runtime-timeout: stdlib/net.goose input and handle validation,
// an ephemeral loopback listener, and IPv6 connect where loopback supports it.
import std;
import net;

fn show_connect(host: const u8[:], port: i64) {
    let s, ok = net::connect(host, port);
    print(s, " ", ok);
}
fn show_listen(host: const u8[:], port: i64) {
    let s, ok = net::listen(host, port);
    print(s, " ", ok);
}

fn main() {
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
    let l, ok = net::listen("127.0.0.1", 0);
    print(ok);                                                // true
    let p, pok = net::local_port(l);
    print(pok, " ", p > 0);                                   // true true
    print(net::send(l, ""));                                  // true
    print(net::close(l));                                     // true
    // A handle that is not a socket: every call fails, recv appends nothing.
    let bad = 999999;
    print(net::send(bad, "x"));                               // false
    var buf: u8[>..] = [];
    print(net::recv(bad, buf), " ", buf.len);                 // -1 0
    let a, aok = net::accept(bad);
    print(a, " ", aok);                                       // 0 false
    let bp, bok = net::local_port(bad);
    print(bp, " ", bok);                                      // 0 false
    print(net::close(bad));                                   // false
    // A handle past the int range too: lookup must refuse it, not read
    // its low bits as a slot index.
    let huge = 4294967296;
    print(net::send(huge, "x"));                             // false
    print(net::recv(huge, buf), " ", buf.len);               // -1 0
    print(net::close(huge));                                 // false
    print(net::close(-huge));                                // false

    // Zero is reserved for failure. A closed handle stays invalid when
    // the runtime reuses its slot for a new listener.
    print(net::close(0));                                    // false
    let next, nok = net::listen("127.0.0.1", 0);
    assert(nok && next != l);
    print(net::close(l));                                    // false
    let next_port, npok = net::local_port(next);
    print(npok && next_port > 0 && net::close(next));         // true

    let v6, v6ok = net::listen("::1", 0);
    if v6ok {
        let v6port, v6pok = net::local_port(v6);
        assert(v6pok);
        let v6client, connected = net::connect("::1", v6port);
        assert(connected);
        assert(net::close(v6client));
        assert(net::close(v6));
    }
}
