# cmake -DINPUT=file -DOUTPUT=header.h -DNAME=symbol -P bin2h.cmake: a file's
# bytes as `static const unsigned char NAME[]` in a header.
file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x..,){24})" "\\1\n    " bytes "${bytes}")
file(WRITE "${OUTPUT}" "/* Generated from ${INPUT} by cmake/bin2h.cmake. */\nstatic const unsigned char ${NAME}[] = {\n    ${bytes}\n};\n")
