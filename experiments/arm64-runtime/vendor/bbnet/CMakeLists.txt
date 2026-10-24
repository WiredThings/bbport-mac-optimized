# Standalone optional module: deliberately does not link or rebuild libbbgpu.
cmake_minimum_required(VERSION 3.24)
project(BloodEchoesOnline LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BBNET_VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/shadps4")
set(COMMON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../metal_api")
find_package(Protobuf CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_path(NLOHMANN_INCLUDE nlohmann/json.hpp REQUIRED)
find_path(MINIUPNPC_INCLUDE miniupnpc.h PATH_SUFFIXES miniupnpc REQUIRED)
find_library(MINIUPNPC_LIBRARY NAMES miniupnpc REQUIRED)
set(PROTO_OUT "${CMAKE_CURRENT_BINARY_DIR}/proto")
file(MAKE_DIRECTORY "${PROTO_OUT}")
add_custom_command(OUTPUT "${PROTO_OUT}/shadnet.pb.cc" "${PROTO_OUT}/shadnet.pb.h"
    COMMAND "${BBNET_PROTOC_EXECUTABLE}" "--proto_path=${BBNET_VENDOR}/shadnet" "--cpp_out=${PROTO_OUT}"
        "${BBNET_VENDOR}/shadnet/shadnet.proto"
    DEPENDS "${BBNET_VENDOR}/shadnet/shadnet.proto")
file(GLOB_RECURSE NETWORK_SOURCES CONFIGURE_DEPENDS
    vendor/shadps4/core/libraries/net/*.cpp vendor/shadps4/core/libraries/network/*.cpp
    vendor/shadps4/core/libraries/np/*.cpp vendor/shadps4/core/net/*.cpp
    vendor/shadps4/shadnet/*.cpp)
list(FILTER NETWORK_SOURCES EXCLUDE REGEX
    "np_trophy|trophy_ui|np_sns_facebook_dialog|np_profile_dialog/|np_commerce/")
file(GLOB THD_SOURCES CONFIGURE_DEPENDS thd/*.cpp)
add_library(bbnet SHARED ${NETWORK_SOURCES} ${THD_SOURCES} "${PROTO_OUT}/shadnet.pb.cc"
    bbnet_glue.cpp mac_module.cpp "${COMMON_ROOT}/shim/common/logging/log.cpp"
    "${COMMON_ROOT}/shim/common/assert.cpp" "${COMMON_ROOT}/shadps4/common/string_util.cpp")
target_include_directories(bbnet PRIVATE "${REPO}/src" include . "${BBNET_VENDOR}" "${BBNET_VENDOR}/shadnet"
    "${PROTO_OUT}" "${NLOHMANN_INCLUDE}" "${MINIUPNPC_INCLUDE}" third_party/httplib
    "${COMMON_ROOT}/shim" "${COMMON_ROOT}/shadps4" "${COMMON_ROOT}/third_party/imgui")
target_compile_definitions(bbnet PRIVATE BOOST_ASIO_STANDALONE NDEBUG
    CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_ZLIB_SUPPORT)
target_include_directories(bbnet PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../aether/src")
target_compile_options(bbnet PRIVATE -Wno-ignored-attributes -fvisibility=hidden -fvisibility-inlines-hidden
    -Wno-deprecated-declarations -Wno-missing-field-initializers)
target_link_libraries(bbnet PRIVATE protobuf::libprotobuf OpenSSL::SSL OpenSSL::Crypto
    ZLIB::ZLIB fmt::fmt Boost::headers magic_enum::magic_enum "${MINIUPNPC_LIBRARY}")
target_link_libraries(bbnet PRIVATE "-framework Security" "-framework CoreFoundation")
set_target_properties(bbnet PROPERTIES INSTALL_NAME_DIR "@rpath")
