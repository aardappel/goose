// error: unknown type: g::Pt
// An alias holds in the file that declares it alone: the imported file
// cannot spell geo::Pt as g::Pt.
namespace g = geo;
import ns_alias.uses_alias;
fn main() { let p = g::Pt { 1 }; print(p.x); }
