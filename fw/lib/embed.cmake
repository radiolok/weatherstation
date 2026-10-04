# cmake -DIN=<file> -DSYM=<symbol> -DOUT=<file.c> -P embed.cmake
file(READ ${IN} hex HEX)
string(LENGTH "${hex}" hexlen)
math(EXPR len "${hexlen} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x..,){16})" "\\1\n" bytes "${bytes}")
file(WRITE ${OUT} "/* Generated from ${IN}, do not edit. */\n"
  "const unsigned char ${SYM}[] = {\n${bytes}0x00\n};\n"
  "const unsigned int ${SYM}_len = ${len};\n")
