// error: a declaration in an extern block is written without extern
extern "SDL2" {
    extern fn SDL_Init(flags: u32) -> i32;
}
fn main() {}
