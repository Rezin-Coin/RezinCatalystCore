# Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
rocksdb_target_header = """load("@fbcode_macros//build_defs:auto_headers.bzl", "AutoHeaders")
load("@fbcode_macros//build_defs:cpp_library.bzl", "cpp_library")
load(":defs.bzl", "test_binary")

REPO_PATH = package_name() + "/"

ROCKSDB_COMPILER_FLAGS = [
    "-fno-builtin-memcmp",
    # Needed to compile in fbcode
    "-Wno-expansion-to-defined",
    # Added missing flags from output of build_detect_platform
    "-Wnarrowing",
    "-DROCKSDB_NO_DYNAMIC_EXTENSION",
]

ROCKSDB_EXTERNAL_DEPS = [
    ("bzip2", None, "bz2"),
    ("snappy", None, "snappy"),
    ("zlib", None, "z"),
    ("gflags", None, "gflags"),
    ("lz4", None, "lz4"),
    ("zstd", None),
    ("tbb", None),
    ("googletest", None, "gtest"),
]

ROCKSDB_OS_DEPS = [
    (
        "linux",
        ["third-party//numa:numa"],
    ),
]

ROCKSDB_OS_PREPROCESSOR_FLAGS = [
    (
        "linux",
        [
            "-DOS_LINUX",
            "-DROCKSDB_FALLOCATE_PRESENT",
            "-DROCKSDB_MALLOC_USABLE_SIZE",
            "-DROCKSDB_PTHREAD_ADAPTIVE_MUTEX",
            "-DROCKSDB_RANGESYNC_PRESENT",
            "-DROCKSDB_SCHED_GETCPU_PRESENT",
            "-DHAVE_SSE42",
            "-DNUMA",
        ],
    ),
    (
        "macos",
        ["-DOS_MACOSX"],
    ),
]

ROCKSDB_PREPROCESSOR_FLAGS = [
    "-DROCKSDB_PLATFORM_POSIX",
    "-DROCKSDB_LIB_IO_POSIX",
    "-DROCKSDB_SUPPORT_THREAD_LOCAL",

    # Flags to enable libs we include
    "-DSNAPPY",
    "-DZLIB",
    "-DBZIP2",
    "-DLZ4",
    "-DZSTD",
    "-DZSTD_STATIC_LINKING_ONLY",
    "-DGFLAGS=gflags",
    "-DTBB",

    # Added missing flags from output of build_detect_platform
    "-DROCKSDB_BACKTRACE",

    # Directories with files for #include
    "-I" + REPO_PATH + "include/",
    "-I" + REPO_PATH,
]

ROCKSDB_ARCH_PREPROCESSOR_FLAGS = {
    "x86_64": [
        "-DHAVE_PCLMUL",
    ],
}

build_mode = read_config("fbcode", "build_mode")

is_opt_mode = build_mode.startswith("opt")

# -DNDEBUG is added by default in opt mode in fbcode. But adding it twice
# doesn't harm and avoid forgetting to add it.
ROCKSDB_COMPILER_FLAGS += (["-DNDEBUG"] if is_opt_mode else [])

sanitizer = read_config("fbcode", "sanitizer")

# Do not enable jemalloc if sanitizer presents. RocksDB will further detect
# whether the binary is linked with jemalloc at runtime.
ROCKSDB_OS_PREPROCESSOR_FLAGS += ([(
    "linux",
    ["-DROCKSDB_JEMALLOC"],
)] if sanitizer == "" else [])

ROCKSDB_OS_DEPS += ([(
    "linux",
    ["third-party//jemalloc:headers"],
)] if sanitizer == "" else [])
"""


library_template = """
cpp_library(
    name = "{name}",
    srcs = [{srcs}],
    {headers_attr_prefix}headers = {headers},
    arch_preprocessor_flags = ROCKSDB_ARCH_PREPROCESSOR_FLAGS,
    compiler_flags = ROCKSDB_COMPILER_FLAGS,
    os_deps = ROCKSDB_OS_DEPS,
    os_preprocessor_flags = ROCKSDB_OS_PREPROCESSOR_FLAGS,
    preprocessor_flags = ROCKSDB_PREPROCESSOR_FLAGS,
    deps = [{deps}],
    external_deps = ROCKSDB_EXTERNAL_DEPS,
)
"""

binary_template = """
cpp_binary(
    name = "%s",
    srcs = [%s],
    arch_preprocessor_flags = ROCKSDB_ARCH_PREPROCESSOR_FLAGS,
    compiler_flags = ROCKSDB_COMPILER_FLAGS,
    preprocessor_flags = ROCKSDB_PREPROCESSOR_FLAGS,
    deps = [%s],
    external_deps = ROCKSDB_EXTERNAL_DEPS,
)
"""

test_cfg_template = """    [
        "%s",
        "%s",
        "%s",
        %s,
        %s,
    ],
"""

unittests_template = """
# [test_name, test_src, test_type, extra_deps, extra_compiler_flags]
ROCKS_TESTS = [
%s]

# Generate a test rule for each entry in ROCKS_TESTS
# Do not build the tests in opt mode, since SyncPoint and other test code
# will not be included.
[
    test_binary(
        extra_compiler_flags = extra_compiler_flags,
        extra_deps = extra_deps,
        parallelism = parallelism,
        rocksdb_arch_preprocessor_flags = ROCKSDB_ARCH_PREPROCESSOR_FLAGS,
        rocksdb_compiler_flags = ROCKSDB_COMPILER_FLAGS,
        rocksdb_external_deps = ROCKSDB_EXTERNAL_DEPS,
        rocksdb_os_deps = ROCKSDB_OS_DEPS,
        rocksdb_os_preprocessor_flags = ROCKSDB_OS_PREPROCESSOR_FLAGS,
        rocksdb_preprocessor_flags = ROCKSDB_PREPROCESSOR_FLAGS,
        test_cc = test_cc,
        test_name = test_name,
    )
    for test_name, test_cc, parallelism, extra_deps, extra_compiler_flags in ROCKS_TESTS
    if not is_opt_mode
]
"""
