// error: embed_shader takes string literals, and let or const globals initialized with one
// In shader, name is the type parameter, which hides the global of its name
// (§11.1).
let name = "probe.frag";

fn shader<name>(x: name) -> i64 { embed_shader(name).len }

fn main() { print(shader(1)); }
