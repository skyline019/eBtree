# Resolve Python3 for test codegen (gen_sql_matrix_inc.py).
set(EBTREE_PYTHON3_ROOT "" CACHE PATH
    "Optional Python 3 root (e.g. d:/anaconda3); used when find_package fails")

if(NOT Python3_EXECUTABLE AND EBTREE_PYTHON3_ROOT)
  if(WIN32)
    set(_ebtree_py_candidate "${EBTREE_PYTHON3_ROOT}/python.exe")
  else()
    set(_ebtree_py_candidate "${EBTREE_PYTHON3_ROOT}/bin/python3")
  endif()
  if(EXISTS "${_ebtree_py_candidate}")
    set(Python3_EXECUTABLE "${_ebtree_py_candidate}" CACHE FILEPATH
        "Python 3 interpreter for eB-Tree build scripts" FORCE)
  endif()
endif()

if(Python3_EXECUTABLE AND EXISTS "${Python3_EXECUTABLE}")
  set(Python3_FOUND TRUE)
else()
  find_package(Python3 COMPONENTS Interpreter QUIET)
endif()

if(EBTREE_BUILD_TESTS AND EBTREE_BUILD_SQL AND NOT Python3_FOUND)
  message(FATAL_ERROR
    "Python 3 required for SQL matrix codegen (gen_sql_matrix_inc.py). "
    "Set -D EBTREE_PYTHON3_ROOT=d:/anaconda3 or -D Python3_EXECUTABLE=... ")
endif()
