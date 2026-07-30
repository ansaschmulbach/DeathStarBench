include("../cmake/Findthrift.cmake")

# NOTE: locally trimmed for a UniqueIdService-only native build (no Docker,
# no Mongo/AMQP on this host) -- not intended to be upstreamed.
find_package(nlohmann_json 3.5.0 REQUIRED)
find_package(Threads)
find_package(OpenSSL REQUIRED)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.54.0 REQUIRED COMPONENTS log log_setup program_options)
if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
  link_directories(${Boost_LIBRARY_DIRS})
endif()

#set(POCO_PREFIX      "/usr/local")
#set(POCO_INCLUDE_DIR "${POCO_PREFIX}/include")
#set(POCO_LIB_DIR     "${POCO_PREFIX}/lib")
#set(POCO_LIBS        "${POCO_LIB_DIR}/libPocoNet.so"
#                     "${POCO_LIB_DIR}/libPocoUtil.so"
#                     "${POCO_LIB_DIR}/libPocoFoundation.so")


add_library(jaegertracing SHARED IMPORTED)
set_target_properties(jaegertracing PROPERTIES IMPORTED_LOCATION
    /usr/local/lib/libjaegertracing.so)

set(THRIFT_GEN_CPP_DIR ../../gen-cpp)

add_subdirectory(UniqueIdService)
add_subdirectory(MediaService)
