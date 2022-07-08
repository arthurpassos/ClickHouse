set(CARES_STATIC ON)
set(CARES_INSTALL OFF)
set(CARES_BUILD_TOOLS OFF)

add_subdirectory("../c-ares/" "../c-ares/")

add_library(ch_contrib::cc-ares ALIAS c-ares-custom)