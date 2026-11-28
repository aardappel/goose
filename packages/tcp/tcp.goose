namespace tcp;

let default_timeout_ms: i64 = 30000;

extern fn tcp_native::connect(host: const u8[:], port: i64, timeout_ms: i64) -> i64 = "goose_tcp_connect";
extern fn tcp_native::listen(host: const u8[:], port: i64) -> i64 = "goose_tcp_listen";
extern fn tcp_native::accept(s: i64, timeout_ms: i64) -> i64 = "goose_tcp_accept";
extern fn tcp_native::take(s: i64) -> u64 = "goose_tcp_take_native";
extern fn tcp_native::local_port(s: i64) -> i64 = "goose_tcp_local_port";
extern fn send(s: i64, data: const u8[:], timeout_ms: i64) -> bool = "goose_tcp_send";
extern fn recv(s: i64, out: u8[>..]&, timeout_ms: i64) -> i64 = "goose_tcp_recv";
extern fn close(s: i64) -> bool = "goose_tcp_close";

fn connect(host: const u8[:], port: i64, timeout_ms: i64) -> i64, bool {
    let s = tcp_native::connect(host, port, timeout_ms);
    if s < 0 { return 0, false; }
    return s, true;
}
fn connect(host: const u8[:], port: i64) -> i64, bool {
    return tcp::connect(host, port, default_timeout_ms);
}

fn listen(host: const u8[:], port: i64) -> i64, bool {
    let s = tcp_native::listen(host, port);
    if s < 0 { return 0, false; }
    return s, true;
}
fn listen(port: i64) -> i64, bool {
    return tcp::listen("", port);
}

fn accept(s: i64, timeout_ms: i64) -> i64, bool {
    let c = tcp_native::accept(s, timeout_ms);
    if c < 0 { return 0, false; }
    return c, true;
}
fn accept(s: i64) -> i64, bool {
    return tcp::accept(s, default_timeout_ms);
}

fn send(s: i64, data: const u8[:]) -> bool {
    return tcp::send(s, data, default_timeout_ms);
}
fn recv(s: i64, out: u8[>..]&) -> i64 {
    return tcp::recv(s, out, default_timeout_ms);
}

fn local_port(s: i64) -> i64, bool {
    let p = tcp_native::local_port(s);
    if p < 0 { return 0, false; }
    return p, true;
}

// Transfers an idle socket to a C adapter, which then owns its descriptor.
fn take_native(s: i64) -> u64, bool {
    let fd = tcp_native::take(s);
    if fd == 0xffffffffffffffff { return 0, false; }
    return fd, true;
}
