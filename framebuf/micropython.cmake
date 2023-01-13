
# Create an INTERFACE library for our C module.
add_library(usermod_framebuf_plus INTERFACE)

# include(${CMAKE_CURRENT_LIST_DIR}/depend.cmake)

set(SRC ${CMAKE_CURRENT_LIST_DIR}/modframebuf.c)
set(GFX_SRC ${CMAKE_CURRENT_LIST_DIR}/gfxfont/gfxfont.c)
file(GLOB ZLIB_SRC ${CMAKE_CURRENT_LIST_DIR}/gfxfont/zlib/*.c)
# file(GLOB ZLIB_SRC ${CMAKE_CURRENT_LIST_DIR}//lib/zlib/*.c)

target_sources(usermod_framebuf_plus INTERFACE
    ${SRC}
    ${GFX_SRC}
    ${ZLIB_SRC}
)

# Add the current directory as an include directory.
target_include_directories(usermod_framebuf_plus INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/gfxfont
    # ${CMAKE_CURRENT_LIST_DIR}/lib
)

# Link our INTERFACE library to the usermod target.
target_link_libraries(usermod INTERFACE usermod_framebuf_plus)
