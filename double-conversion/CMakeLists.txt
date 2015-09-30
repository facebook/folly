
set(headers
  bignum.h
  cached-powers.h
  diy-fp.h
  double-conversion.h
  fast-dtoa.h
  fixed-dtoa.h
  ieee.h
  strtod.h
  utils.h
  )

add_library(double-conversion
bignum.cc
bignum-dtoa.cc
cached-powers.cc
diy-fp.cc
double-conversion.cc
fast-dtoa.cc
fixed-dtoa.cc
strtod.cc
${headers}
)

#
# associates the list of headers with the library
# for the purposes of installation/import into other projects
set_target_properties(double-conversion
    PROPERTIES PUBLIC_HEADER "${headers}")

if (BUILD_SHARED_LIBS)
  set_target_properties(double-conversion
    PROPERTIES VERSION ${double-conversion_SOVERSION}
               SOVERSION ${double-conversion_SOVERSION_MAJOR})
endif()

#
# install command to set up library install
# given the above PUBLIC_HEADER property set, this
# pulls along all the header files with the library.
install(TARGETS double-conversion
  EXPORT double-conversionLibraryDepends
  RUNTIME DESTINATION "${INSTALL_BIN_DIR}" COMPONENT bin
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}/lib" COMPONENT shlib
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}/lib" COMPONENT lib
  PUBLIC_HEADER DESTINATION "${INSTALL_INCLUDE_DIR}/include/double-conversion"
  COMPONENT dev)
