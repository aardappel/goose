// The entry of the package greet. The sourced import is a requirement: the
// root of a program using greet declares tools itself (§4.3).
namespace greet;
import std;
import tools from "../tools";
import .internal;
import helper;

fn hello(name: const u8[:]) -> u8[>..] {
    return str("hello, ", name, " ", internal_code(), " ", helper_code(), " ", tools::twice(21));
}
