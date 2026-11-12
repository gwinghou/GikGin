set(LIBGIKGGIN_API_VERSION "0.1.0")

set(LIBGIKGGIN_ABI_VERSION "0.1.0")
set(LIBGIKGGIN_ABI_MAJOR_VERSION "0")

set(GIKGGIN_SRC
	dictbuilder.cpp
	dictlist.cpp
	dicttrie.cpp
	lpicache.cpp
	matrixsearch.cpp
	mystdlib.cpp
	ngram.cpp
	pinyinime.cpp
	searchutility.cpp
	spellingtable.cpp
	spellingtrie.cpp
	splparser.cpp
	sync.cpp
	userdict.cpp
	utf16char.cpp
	utf16reader.cpp
)

add_library(gikgin SHARED ${GIKGGIN_SRC})
target_link_libraries(gikgin pthread )
set_target_properties(gikgin PROPERTIES
    VERSION ${LIBGIKGGIN_ABI_VERSION}
    SOVERSION ${LIBGIKGGIN_ABI_MAJOR_VERSION}
    LINK_FLAGS ${CMAKE_SHARED_LINKER_FLAGS}
)

if (ENABLE_STATIC)

add_library(gikgin-static STATIC ${GIKGGIN_SRC})
SET_TARGET_PROPERTIES(gikgin-static PROPERTIES
    OUTPUT_NAME "gikgin"
    PREFIX "lib"
)

install(TARGETS gikgin-static
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
    ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
)
endif (ENABLE_STATIC)
install(TARGETS gikgin
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
    ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
)
