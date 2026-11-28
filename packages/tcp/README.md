# TCP package for Goose

An opt-in TCP package for Linux, macOS and Windows. A program beside this
directory imports it with `import tcp from "./tcp";`. Both `goose build` and
`goose run` use a C toolchain and cache the native build. The package sidecar
links Winsock on Windows. Programs that omit the import have no TCP dependency.
The `tcp` namespace can be used alongside gslib's `net`.

```goose
import tcp from "./tcp";

fn main() {
    let listener, ok = tcp::listen("127.0.0.1", 8080);
    if !ok { return; }
    defer tcp::close(listener);
    let peer, accepted = tcp::accept(listener, 5000);
    if !accepted { return; }
    defer tcp::close(peer);
    var data: u8[>..] = [];
    if tcp::recv(peer, data, 2000) > 0 {
        tcp::send(peer, data, 2000);
    }
}
```

- `listen(host, port)` binds a resolved address. `listen(port)` tries an IPv6
  dual-stack wildcard first and falls back to IPv4. Use port 0 and
  `local_port(listener)` for an OS-assigned port.
- `connect`, `accept`, `send` and `recv` take an optional final timeout in
  milliseconds. The default is 30 seconds. Use 0 for an immediate attempt
  or -1 to wait without a deadline. The connect budget covers TCP connection
  attempts after synchronous name resolution.
- `recv` appends up to 65536 bytes and returns the number appended. It
  returns 0 at orderly EOF, -2 on timeout, or -1 on cancellation or I/O
  failure. Read in a loop when an application expects a complete message.
  `send` returns true after sending all bytes. A false result can follow a
  partial send.
- Socket handles are opaque integers that can cross thread queues.
  `close` consumes a valid handle immediately. Pending package operations
  check for cancellation between readiness waits of at most 50 ms. The last
  pending operation closes the OS socket. Callers close their own handles.
- `take_native(handle)` transfers an idle socket into C ownership and
  invalidates the Goose handle. It returns a `u64, bool`. The adapter casts
  the value to a POSIX file descriptor or Windows `SOCKET` and takes
  responsibility for waiting on, configuring and closing that nonblocking
  socket. This supports C-side polling, socket options and TLS adapters.

Run the loopback tests with `python3 test/run.py /path/to/goose` from this
directory. The runner checks native and JIT execution and caps each process.
