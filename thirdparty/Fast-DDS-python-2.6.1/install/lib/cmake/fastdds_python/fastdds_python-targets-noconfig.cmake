#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "fastdds_python" for configuration ""
set_property(TARGET fastdds_python APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(fastdds_python PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/python3.10/site-packages/fastdds/_fastdds_python.so"
  IMPORTED_SONAME_NOCONFIG "_fastdds_python.so"
  )

list(APPEND _cmake_import_check_targets fastdds_python )
list(APPEND _cmake_import_check_files_for_fastdds_python "${_IMPORT_PREFIX}/lib/python3.10/site-packages/fastdds/_fastdds_python.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
