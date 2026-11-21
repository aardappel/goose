// The Goose standard library: `net` (docs/stdlib.md): a thin layer over the
// C in src/runtime/runtime_net.h (spec §7.10) -- blocking TCP, enough to
// write clients and servers. Handles are i64, flat, so they cross queues
// (§11.2); two-outcome calls return a trailing bool (§2.2 of the design).

namespace net;

import std;

// Socket handles are opaque. Network I/O blocks without a timeout. A
// contained worker that aborts leaves its sockets open (spec §11.2).
extern fn net_detail::connect(host: const u8[:], port: i64) -> i64 = "gs_net_connect";
extern fn net_detail::listen(host: const u8[:], port: i64) -> i64 = "gs_net_listen";
extern fn net_detail::accept(s: i64) -> i64 = "gs_net_accept";
extern fn net_detail::local_port(s: i64) -> i64 = "gs_net_local_port";
extern fn send(s: i64, data: const u8[:]) -> bool = "gs_net_send";  // all bytes or false; some may already be sent
extern fn recv(s: i64, out: u8[>..]&) -> i64 = "gs_net_recv";       // n bytes appended; 0 at end; -1 on error
extern fn close(s: i64) -> bool = "gs_net_close";                 // false while an operation is using s

// Resolves `host` -- a DNS name or a numeric IPv4/IPv6 literal -- and tries
// each address until one connects. Port 1..65535.
fn connect(host: const u8[:], port: i64) -> i64, bool {
    let s = net_detail::connect(host, port);
    if s < 0 { return 0, false; }
    return s, true;
}

// Binds dual-stack any; port 0 picks an ephemeral port, which local_port
// reports. A wildcard bind can raise an OS firewall prompt.
fn listen(port: i64) -> i64, bool {
    let s, ok = listen("", port);
    return s, ok;
}
// Binds the resolved address instead; port 0..65535.
fn listen(host: const u8[:], port: i64) -> i64, bool {
    let s = net_detail::listen(host, port);
    if s < 0 { return 0, false; }
    return s, true;
}

// One pending connection on a listening socket.
fn accept(s: i64) -> i64, bool {
    let c = net_detail::accept(s);
    if c < 0 { return 0, false; }
    return c, true;
}

// The port a socket is bound to -- the way to use port 0.
fn local_port(s: i64) -> i64, bool {
    let p = net_detail::local_port(s);
    if p < 0 { return 0, false; }
    return p, true;
}
