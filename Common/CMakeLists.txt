cmake_minimum_required(VERSION 3.20)
add_definitions(-D_WIN32_WINNT=0x0601)
cmake_policy(SET CMP0167 NEW)
cmake_policy(SET CMP0074 NEW)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# Common 公共库
project(Common)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用已有的 message.pb.cc 和 message.grpc.pb.cc
set(GENERATED_PROTO_FILES
        src/message.pb.cc
        src/message.grpc.pb.cc
)
include_directories("D:/environment/Redis/deps/hiredis")
link_directories("D:/environment/Redis/lib")
# Boost
set(CMAKE_PREFIX_PATH "D:/environment/boost_1_88_0/boost_1_88_0/lib/cmake")
find_package(Boost 1.88 REQUIRED COMPONENTS thread system filesystem)
# Jsoncpp
set(JSONCPP_INCLUDE_DIR "D:/environment/jsoncpp-1.9.6/include")
set(JSONCPP_LIB_DIR "D:/environment/jsoncpp-1.9.6/build/lib/Debug")

# MySQL Connector/C++
set(MYSQLCPP_ROOT "D:/environment/mysql-connector/mysql-connector-c++-9.4.0-winx64")

# 添加 gRPC 源码路径
set(GRPC_PROTOBUF_INCLUDE_DIR "D:/environment/grpc/third_party/protobuf/src")
set(GRPC_INCLUDE_DIR "D:/environment/grpc/include")
set(ABSL_INCLUDE_DIR "D:/environment/grpc/third_party/abseil-cpp")
set(GRPC_ADDRESS_SORTING_INCLUDE_DIR "D:/environment/grpc/third_party/address_sorting/include")
set(GRPC_RE2_INCLUDE_DIR "D:/environment/grpc/third_party/re2")

set(GRPC_LIB_DIR "D:/environment/grpc/visual_pro/Debug")
set(GRPC_RE2_LIB_DIR "D:/environment/grpc/visual_pro/third_party/re2/Debug")
set(GRPC_PROTOBUF_DIR "D:/environment/grpc/visual_pro/third_party/protobuf/Debug")
set(GRPC_ABSL_BASE_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/base/Debug")
set(GRPC_ABSL_CONTAINER_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/container/Debug")
set(GRPC_ABSL_CRC_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/crc/Debug")
set(GRPC_ABSL_DEBUGGING_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/debugging/Debug")
set(GRPC_ABSL_FLAGS_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/flags/Debug")
set(GRPC_ABSL_HASH_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/hash/Debug")
set(GRPC_ABSL_LOG_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/log/Debug")
set(GRPC_ABSL_NUMERIC_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/numeric/Debug")
set(GRPC_ABSL_PROFILING_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/profiling/Debug")
set(GRPC_ABSL_RANDOM_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/random/Debug")
set(GRPC_ABSL_STATUS_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/status/Debug")
set(GRPC_ABSL_STRINGS_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/strings/Debug")
set(GRPC_ABSL_SYNCHRONIZATION_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/synchronization/Debug")
set(GRPC_ABSL_TYPES_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/types/Debug")
set(GRPC_ABSL_TIME_DIR "D:/environment/grpc/visual_pro/third_party/abseil-cpp/absl/time/Debug")
set(GRPC_BORINGSSL_DIR "D:/environment/grpc/visual_pro/third_party/boringssl-with-bazel/Debug")
set(GRPC_CARES_DIR "D:/environment/grpc/visual_pro/third_party/cares/cares/src/lib/Debug")
set(GRPC_PROTOBUF_UTF8_RANGE_DIR "D:/environment/grpc/visual_pro/third_party/protobuf/third_party/utf8_range/Debug")
set(GRPC_ZLIB_DIR "D:/environment/grpc/visual_pro/third_party/zlib/Debug")

set(ABSL_LIBS
        # ---------- gRPC 核心 ----------
        ${GRPC_LIB_DIR}/grpc++.lib
        ${GRPC_LIB_DIR}/grpc.lib
        ${GRPC_LIB_DIR}/gpr.lib
        ${GRPC_LIB_DIR}/grpc++_reflection.lib
        ${GRPC_LIB_DIR}/address_sorting.lib

        # ---------- Protobuf ----------
        ${GRPC_PROTOBUF_DIR}/libprotobufd.lib

        # ---------- utf8_range ----------
        ${GRPC_PROTOBUF_UTF8_RANGE_DIR}/libutf8_range.lib
        ${GRPC_PROTOBUF_UTF8_RANGE_DIR}/libutf8_validity.lib

        # ---------- cares ----------
        ${GRPC_CARES_DIR}/cares.lib

        # ---------- zlib ----------
        ${GRPC_ZLIB_DIR}/zlibstaticd.lib

        # ---------- boringSSL ----------
        ${GRPC_BORINGSSL_DIR}/ssl.lib
        ${GRPC_BORINGSSL_DIR}/crypto.lib

        # ---------- RE2 ----------
        ${GRPC_RE2_LIB_DIR}/re2.lib

        # ---------- upb (位于 grpc build 目录) ----------
        # 注意：upb 库文件统一放在 ${GRPC_LIB_DIR}；若实际路径不同请自行调整
        ${GRPC_LIB_DIR}/upb_base_lib.lib
        ${GRPC_LIB_DIR}/upb_hash_lib.lib
        ${GRPC_LIB_DIR}/upb_json_lib.lib
        ${GRPC_LIB_DIR}/upb_lex_lib.lib
        ${GRPC_LIB_DIR}/upb_mem_lib.lib
        ${GRPC_LIB_DIR}/upb_message_lib.lib
        ${GRPC_LIB_DIR}/upb_mini_descriptor_lib.lib
        ${GRPC_LIB_DIR}/upb_mini_table_lib.lib
        ${GRPC_LIB_DIR}/upb_reflection_lib.lib
        ${GRPC_LIB_DIR}/upb_textformat_lib.lib
        ${GRPC_LIB_DIR}/upb_wire_lib.lib

        # ---------- abseil (absl) ----------
        # base
        ${GRPC_ABSL_BASE_DIR}/absl_base.lib
        ${GRPC_ABSL_BASE_DIR}/absl_throw_delegate.lib
        ${GRPC_ABSL_BASE_DIR}/absl_raw_logging_internal.lib
        ${GRPC_ABSL_BASE_DIR}/absl_log_severity.lib          # 仍存在于 20240116 LTS
        ${GRPC_ABSL_BASE_DIR}/absl_spinlock_wait.lib
        ${GRPC_ABSL_BASE_DIR}/absl_strerror.lib

        # city / hash / low_level_hash
        ${GRPC_ABSL_HASH_DIR}/absl_city.lib
        ${GRPC_ABSL_HASH_DIR}/absl_hash.lib
        ${GRPC_ABSL_HASH_DIR}/absl_low_level_hash.lib
        ${GRPC_ABSL_CONTAINER_DIR}/absl_hashtablez_sampler.lib
        ${GRPC_ABSL_CONTAINER_DIR}/absl_raw_hash_set.lib

        # strings
        ${GRPC_ABSL_STRINGS_DIR}/absl_strings.lib
        ${GRPC_ABSL_STRINGS_DIR}/absl_strings_internal.lib
        ${GRPC_ABSL_STRINGS_DIR}/absl_str_format_internal.lib
        ${GRPC_ABSL_STRINGS_DIR}/absl_string_view.lib

        # cord (内部依赖)
        ${GRPC_ABSL_STRINGS_DIR}/absl_cord.lib
        ${GRPC_ABSL_STRINGS_DIR}/absl_cord_internal.lib
        ${GRPC_ABSL_STRINGS_DIR}/absl_cordz_functions.lib
        ${GRPC_ABSL_STRINGS_DIR}/absl_cordz_handle.lib
        ${GRPC_ABSL_STRINGS_DIR}/absl_cordz_info.lib
        ${GRPC_ABSL_STRINGS_DIR}/absl_cordz_sample_token.lib

        # synchronization
        ${GRPC_ABSL_SYNCHRONIZATION_DIR}/absl_synchronization.lib
        ${GRPC_ABSL_SYNCHRONIZATION_DIR}/absl_graphcycles_internal.lib
        ${GRPC_ABSL_SYNCHRONIZATION_DIR}/absl_kernel_timeout_internal.lib

        # debugging / symbolize / demangle
        ${GRPC_ABSL_DEBUGGING_DIR}/absl_debugging_internal.lib
        ${GRPC_ABSL_DEBUGGING_DIR}/absl_demangle_internal.lib
        ${GRPC_ABSL_DEBUGGING_DIR}/absl_examine_stack.lib
        ${GRPC_ABSL_DEBUGGING_DIR}/absl_failure_signal_handler.lib
        ${GRPC_ABSL_DEBUGGING_DIR}/absl_stacktrace.lib
        ${GRPC_ABSL_DEBUGGING_DIR}/absl_symbolize.lib
        ${GRPC_ABSL_DEBUGGING_DIR}/absl_leak_check.lib

        # malloc / scoped_set_env
        ${GRPC_ABSL_BASE_DIR}/absl_malloc_internal.lib
        ${GRPC_ABSL_BASE_DIR}/absl_scoped_set_env.lib

        # bad_*_access
        # ${GRPC_ABSL_BASE_DIR}/absl_bad_any_cast_impl.lib
        # ${GRPC_ABSL_BASE_DIR}/absl_bad_optional_access.lib
        # ${GRPC_ABSL_BASE_DIR}/absl_bad_variant_access.lib

        # crc
        ${GRPC_ABSL_CRC_DIR}/absl_crc32c.lib
        ${GRPC_ABSL_CRC_DIR}/absl_crc_cord_state.lib
        ${GRPC_ABSL_CRC_DIR}/absl_crc_internal.lib

        # numeric / int128
        ${GRPC_ABSL_NUMERIC_DIR}/absl_int128.lib

        # flags
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_commandlineflag.lib
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_commandlineflag_internal.lib
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_config.lib
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_internal.lib
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_marshalling.lib
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_parse.lib
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_private_handle_accessor.lib
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_program_name.lib
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_reflection.lib
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_usage.lib
        ${GRPC_ABSL_FLAGS_DIR}/absl_flags_usage_internal.lib

        # log
        # ${GRPC_ABSL_LOG_DIR}/absl_log_entry.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_flags.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_globals.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_initialize.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_sink.lib

        # log_internal
        ${GRPC_ABSL_LOG_DIR}/absl_log_internal_check_op.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_internal_conditions.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_internal_fnmatch.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_internal_format.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_internal_globals.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_internal_log_sink_set.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_internal_message.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_internal_nullguard.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_internal_proto.lib
        ${GRPC_ABSL_LOG_DIR}/absl_log_internal_structured_proto.lib
        ${GRPC_ABSL_LOG_DIR}/absl_vlog_config_internal.lib

        # random
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_distributions.lib
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_internal_distribution_test_util.lib
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_internal_platform.lib
        # ${GRPC_ABSL_RANDOM_DIR}/absl_random_internal_pool_urbg.lib
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_internal_entropy_pool.lib
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_internal_randen.lib
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_internal_randen_hwaes.lib
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_internal_randen_hwaes_impl.lib
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_internal_randen_slow.lib
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_internal_seed_material.lib
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_seed_gen_exception.lib
        ${GRPC_ABSL_RANDOM_DIR}/absl_random_seed_sequences.lib

        # status
        ${GRPC_ABSL_STATUS_DIR}/absl_status.lib
        ${GRPC_ABSL_STATUS_DIR}/absl_statusor.lib

        # time / civil_time / time_zone
        ${GRPC_ABSL_TIME_DIR}/absl_time.lib
        ${GRPC_ABSL_TIME_DIR}/absl_civil_time.lib
        ${GRPC_ABSL_TIME_DIR}/absl_time_zone.lib

        # periodic_sampler
        ${GRPC_ABSL_PROFILING_DIR}/absl_periodic_sampler.lib
        ${GRPC_ABSL_PROFILING_DIR}/absl_exponential_biased.lib
)
# ---- 静态库 Common ----
add_library(Common STATIC
        src/ConfigMgr.cpp
        src/AsioIOServicePool.cpp
        src/StatusGrpcClient.cpp
        src/MysqlDao.cpp
        src/RedisMgr.cpp
        include/message.grpc.pb.h
        include/message.pb.h
        ${GENERATED_PROTO_FILES}
)

# ---- include 路径：Common 的头文件给所有依赖者可见 ----
target_include_directories(Common
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
        ${JSONCPP_INCLUDE_DIR}
        ${GRPC_PROTOBUF_INCLUDE_DIR}  # protobuf头文件
        ${GRPC_INCLUDE_DIR}  # grpc头文件
        ${ABSL_INCLUDE_DIR}  # abseil头文件（核心，包含所有absl/...子目录）
        ${GRPC_ADDRESS_SORTING_INCLUDE_DIR}
        ${GRPC_RE2_INCLUDE_DIR}
        ${MYSQLCPP_ROOT}/include
)

# ---- 链接 protobuf / grpc ----
target_link_libraries(Common
        PUBLIC
        D:/environment/grpc/visual_pro/Debug/grpc++.lib
        D:/environment/grpc/visual_pro/Debug/grpc.lib
        Boost::thread
        Boost::system
        Boost::filesystem
        ${JSONCPP_LIB_DIR}/jsoncpp.lib
        ${ABSL_LIBS}
        hiredis
        Win32_Interop
        mysqlcppconn
)

# ---- 编译选项 ----
target_compile_features(Common PUBLIC cxx_std_17)
