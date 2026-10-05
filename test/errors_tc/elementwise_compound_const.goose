// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
import vec;
fn change(v: const float3&) { v += float3_1; }
fn main() { let v = float3_0; change(v); }
