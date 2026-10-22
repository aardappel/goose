// lsp: fixture for test/lsp_protocol.py: a module two_a.goose and
// two_b.goose import, both entry files of a client of their own.
namespace two_shared;

fn twice(x: i64) -> i64 { x * 2 }

fn half(x: i64) -> i64 { x / 2 }
fn half(x: f64) -> f64 { x / 2.0 }

// Instantiated by two_b.goose alone, with an f64. Its exemplar in
// two_a.goose's analysis ends before the call of half, which that analysis
// takes for the first half by its name alone.
fn apply<T>(x: T) -> f64 { let y: f64 = x; half(y) }
