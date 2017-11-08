find_package(Boost 1.55.0 MODULE
  COMPONENTS
    context
    chrono
    date_time
    filesystem
    program_options
    regex
    system
    thread
  REQUIRED
)

find_package(DoubleConversion MODULE REQUIRED)

find_package(gflags CONFIG)
if(NOT TARGET gflags)
  find_package(GFlags MODULE REQUIRED)
endif()

find_package(glog CONFIG)
if(NOT TARGET glog::glog)
  find_package(GLog MODULE REQUIRED)
endif()

find_package(Libevent CONFIG)
if(NOT TARGET event)
  find_package(LibEvent MODULE REQUIRED)
endif()

find_package(OpenSSL MODULE REQUIRED)
find_package(PThread MODULE)
