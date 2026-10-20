// error: expected ';' in extern declaration, found '{'
extern "SDL2" {
    fn SDL_Init(flags: u32) -> i32 { 0 }
}
fn main() {}
