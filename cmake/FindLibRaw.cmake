find_path(LIBRAW_INCLUDE_DIR
    NAMES libraw/libraw.h
    HINTS /opt/homebrew/include
)

find_library(LIBRAW_LIBRARY
    NAMES raw raw_r
    HINTS /opt/homebrew/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibRaw DEFAULT_MSG LIBRAW_LIBRARY LIBRAW_INCLUDE_DIR)

if(LIBRAW_FOUND AND NOT TARGET LibRaw::LibRaw)
    add_library(LibRaw::LibRaw UNKNOWN IMPORTED)
    set_target_properties(LibRaw::LibRaw PROPERTIES
        IMPORTED_LOCATION "${LIBRAW_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBRAW_INCLUDE_DIR}"
    )
endif()
