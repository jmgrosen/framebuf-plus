
# Create an INTERFACE library for our C module.
add_library(usermod_framebuf_plus INTERFACE)

# include(${CMAKE_CURRENT_LIST_DIR}/depend.cmake)

# mod
set(MOD_DIR ${CMAKE_CURRENT_LIST_DIR})
set(MOD_SRC ${MOD_DIR}/modframebuf.c)
set(MOD_INC ${MOD_DIR})

# gfx font
set(GFX_DIR ${CMAKE_CURRENT_LIST_DIR}/gfxfont)
set(GFX_SRC ${GFX_DIR}/gfxfont.c)
set(GFX_INC ${GFX_DIR})

file(GLOB ZLIB_SRC ${GFX_DIR}/zlib/*.c)
list(APPEND GFX_SRC ${ZLIB_SRC})

# jpg
set(JPG_DIR ${CMAKE_CURRENT_LIST_DIR}/tjpgd)
set(JPG_SRC ${JPG_DIR}/tjpgd.c)
set(JPG_INC ${JPG_DIR})

target_sources(usermod_framebuf_plus INTERFACE
    ${MOD_SRC}
    ${GFX_SRC}
    ${JPG_SRC}
)

# Add the current directory as an include directory.
target_include_directories(usermod_framebuf_plus INTERFACE
    ${MOD_INC}
    ${GFX_INC}
    ${JPG_INC}
)

# Link our INTERFACE library to the usermod target.
target_link_libraries(usermod INTERFACE usermod_framebuf_plus)
