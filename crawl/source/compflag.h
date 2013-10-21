#ifndef __included_crawl_compile_flags_h
#define __included_crawl_compile_flags_h

#define CRAWL_CFLAGS "-O2 -pipe -Wall -Wformat-security -Wmissing-declarations -DWINMM_PLAY_SOUNDS -DUSE_TILE -DUSE_TILE_LOCAL -DUSE_SDL -DUSE_GL -DUSE_FT -Wundef -Wno-array-bounds -Wno-parentheses -Wno-unused-parameter -Wwrite-strings -Wshadow -Wuninitialized -Icontrib/install/mingw32/include -Iutil -I. -Irltiles -Icontrib/install/mingw32/include/SDL -Icontrib/install/mingw32/include/freetype2 -DWIZARD -DASSERTS -DPROPORTIONAL_FONT=\"contrib/fonts/DejaVuSans.ttf\" -DMONOSPACED_FONT=\"contrib/fonts/DejaVuSansMono.ttf\" -DREGEX_PCRE -DCLUA_BINDINGS"
#define CRAWL_LDFLAGS "-O2 "
#define CRAWL_HOST "mingw32"
#define CRAWL_ARCH "mingw32"

#endif

