// error: extern fn take: parameter p of type Mixed cannot cross to C: a struct with pad passes by value
// By value, a pad is bytes the calling convention passes where the library's
// own struct has padding: { float; double; } goes in two float registers on
// x86-64 System V, this in an integer one. By reference it crosses. (spec §7.10)
struct Mixed { x: f32, pad 4, y: f64 }
extern "foreign" {
    fn take_ref(p: const Mixed&) -> f64;
    fn take(p: Mixed) -> f64;
}
fn main() {}
