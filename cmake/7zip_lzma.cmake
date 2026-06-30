# 7-Zip LZMA C library (Public Domain) — minimal LzmaLib subset for eB-Tree.

if(NOT CMAKE_C_COMPILER_LOADED)
  enable_language(C)
endif()

set(_7ZIP_C_DIR "${CMAKE_SOURCE_DIR}/7zip-main/C")

set(EBTREE_LZMA_SOURCES
  "${_7ZIP_C_DIR}/Alloc.c"
  "${_7ZIP_C_DIR}/CpuArch.c"
  "${_7ZIP_C_DIR}/LzFind.c"
  "${_7ZIP_C_DIR}/LzFindOpt.c"
  "${_7ZIP_C_DIR}/LzFindMt.c"
  "${_7ZIP_C_DIR}/LzmaDec.c"
  "${_7ZIP_C_DIR}/LzmaEnc.c"
  "${_7ZIP_C_DIR}/LzmaLib.c"
  "${_7ZIP_C_DIR}/Threads.c"
)

add_library(ebtree_lzma STATIC ${EBTREE_LZMA_SOURCES})

target_include_directories(ebtree_lzma
  PUBLIC
    "${_7ZIP_C_DIR}"
)

target_compile_definitions(ebtree_lzma PUBLIC Z7_NO_UNICODE=1)

if(MSVC)
  target_compile_options(ebtree_lzma PRIVATE /W3)
  target_link_libraries(ebtree_lzma PUBLIC winmm)
else()
  target_link_libraries(ebtree_lzma PUBLIC pthread)
endif()
