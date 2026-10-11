# subprocess.h

`subprocess.h`, copied unmodified from
[sheredom/subprocess.h](https://github.com/sheredom/subprocess.h) at commit
`8a4715c126433c0f55abe7c10431830864a18e74` (fetched 2026-10-10).

A single-header library for starting a process with a working directory and
an environment, reading what it prints and waiting for its exit code, on
Windows, Linux and macOS. `src/process.cpp` builds the compiler's process
runner on it: the C compiler, cmake and git that `goose build` and packages
call.

License: public domain (the Unlicense), the full text at the top of the file.

To update, copy the file from a newer commit and record that commit here.
