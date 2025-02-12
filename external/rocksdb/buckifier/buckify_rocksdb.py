# Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
from targets_builder import TARGETSBuilder
import json
import os
import fnmatch
import sys

from util import ColorString

# This script generates TARGETS file for Buck.
# Buck is a build tool specifying dependencies among different build targets.
# User can pass extra dependencies as a JSON object via command line, and this
# script can include these dependencies in the generate TARGETS file.
# Usage:
# $python buckifier/buckify_rocksdb.py
# (This generates a TARGET file without user-specified dependency for unit
# tests.)
# $python buckifier/buckify_rocksdb.py \
#        '{"fake": { \
#                      "extra_deps": [":test_dep", "//fakes/module:mock1"],  \
#                      "extra_compiler_flags": ["-DROCKSDB_LITE", "-Os"], \
#                  } \
#         }'
# (Generated TARGETS file has test_dep and mock1 as dependencies for RocksDB
# unit tests, and will use the extra_compiler_flags to compile the unit test
# source.)

# tests to export as libraries for inclusion in other projects
_EXPORTED_TEST_LIBS = ["env_basic_test"]

# Parse src.mk files as a Dictionary of
# VAR_NAME => list of files
def parse_src_mk(repo_path):
    src_mk = repo_path + "/src.mk"
    src_files = {}
    for line in open(src_mk):
        line = line.strip()
        if len(line) == 0 or line[0] == '#':
            continue
        if '=' in line:
            current_src = line.split('=')[0].strip()
            src_files[current_src] = []
        elif '.cc' in line:
            src_path = line.split('.cc')[0].strip() + '.cc'
            src_files[current_src].append(src_path)
    return src_files


# get all .cc / .c files
def get_cc_files(repo_path):
    cc_files = []
    for root, dirnames, filenames in os.walk(repo_path):  # noqa: B007 T25377293 Grandfathered in
        root = root[(len(repo_path) + 1):]
        if "java" in root:
            # Skip java
            continue
        for filename in fnmatch.filter(filenames, '*.cc'):
            cc_files.append(os.path.join(root, filename))
        for filename in fnmatch.filter(filenames, '*.c'):
            cc_files.append(os.path.join(root, filename))
    return cc_files


# Get tests from Makefile
def get_tests(repo_path):
    Makefile = repo_path + "/Makefile"

    # Dictionary TEST_NAME => IS_PARALLEL
    tests = {}

    found_tests = False
    for line in open(Makefile):
        line = line.strip()
        if line.startswith("TESTS ="):
            found_tests = True
        elif found_tests:
            if line.endswith("\\"):
                # remove the trailing \
                line = line[:-1]
                line = line.strip()
                tests[line] = False
            else:
                # we consumed all the tests
                break

    found_parallel_tests = False
    for line in open(Makefile):
        line = line.strip()
        if line.startswith("PARALLEL_TEST ="):
            found_parallel_tests = True
        elif found_parallel_tests:
            if line.endswith("\\"):
                # remove the trailing \
                line = line[:-1]
                line = line.strip()
                tests[line] = True
            else:
                # we consumed all the parallel tests
                break

    return tests


# Parse extra dependencies passed by user from command line
def get_dependencies():
    deps_map = {
        ''.encode('ascii'): {
            'extra_deps'.encode('ascii'): [],
            'extra_compiler_flags'.encode('ascii'): []
        }
    }
    if len(sys.argv) < 2:
        return deps_map

    def encode_dict(data):
        rv = {}
        for k, v in data.items():
            if isinstance(k, unicode):
                k = k.encode('ascii')
            if isinstance(v, unicode):
                v = v.encode('ascii')
            elif isinstance(v, list):
                v = [x.encode('ascii') for x in v]
            elif isinstance(v, dict):
                v = encode_dict(v)
            rv[k] = v
        return rv
    extra_deps = json.loads(sys.argv[1], object_hook=encode_dict)
    for target_alias, deps in extra_deps.items():
        deps_map[target_alias] = deps
    return deps_map


# Prepare TARGETS file for buck
def generate_targets(repo_path, deps_map):
    print(ColorString.info("Generating TARGETS"))
    # parsed src.mk file
    src_mk = parse_src_mk(repo_path)
    # get all .cc files
    cc_files = get_cc_files(repo_path)
    # get tests from Makefile
    tests = get_tests(repo_path)

    if src_mk is None or cc_files is None or tests is None:
        return False

    TARGETS = TARGETSBuilder("%s/TARGETS" % repo_path)
    # rocksdb_lib
    TARGETS.add_library(
        "rocksdb_lib",
        src_mk["LIB_SOURCES"] +
        src_mk["TOOL_LIB_SOURCES"])
    # rocksdb_test_lib
    TARGETS.add_library(
        "rocksdb_test_lib",
        src_mk.get("MOCK_LIB_SOURCES", []) +
        src_mk.get("TEST_LIB_SOURCES", []) +
        src_mk.get("EXP_LIB_SOURCES", []) +
        src_mk.get("ANALYZER_LIB_SOURCES", []),
        [":rocksdb_lib"])
    # rocksdb_tools_lib
    TARGETS.add_library(
        "rocksdb_tools_lib",
        src_mk.get("BENCH_LIB_SOURCES", []) +
        src_mk.get("ANALYZER_LIB_SOURCES", []) +
        ["test_util/testutil.cc"],
        [":rocksdb_lib"])

    print("Extra dependencies:\n{0}".format(str(deps_map)))
    # test for every test we found in the Makefile
    for target_alias, deps in deps_map.items():
        for test in sorted(tests):
            match_src = [src for src in cc_files if ("/%s.c" % test) in src]
            if len(match_src) == 0:
                print(ColorString.warning("Cannot find .cc file for %s" % test))
                continue
            elif len(match_src) > 1:
                print(ColorString.warning("Found more than one .cc for %s" % test))
                print(match_src)
                continue

            assert(len(match_src) == 1)
            is_parallel = tests[test]
            test_target_name = \
                test if not target_alias else test + "_" + target_alias
            TARGETS.register_test(
                test_target_name,
                match_src[0],
                is_parallel,
                deps['extra_deps'],
                deps['extra_compiler_flags'])

            if test in _EXPORTED_TEST_LIBS:
                test_library = "%s_lib" % test_target_name
                TARGETS.add_library(test_library, match_src, [":rocksdb_test_lib"])
    TARGETS.flush_tests()

    print(ColorString.info("Generated TARGETS Summary:"))
    print(ColorString.info("- %d libs" % TARGETS.total_lib))
    print(ColorString.info("- %d binarys" % TARGETS.total_bin))
    print(ColorString.info("- %d tests" % TARGETS.total_test))
    return True


def get_rocksdb_path():
    # rocksdb = {script_dir}/..
    script_dir = os.path.dirname(sys.argv[0])
    script_dir = os.path.abspath(script_dir)
    rocksdb_path = os.path.abspath(
        os.path.join(script_dir, "../"))

    return rocksdb_path

def exit_with_error(msg):
    print(ColorString.error(msg))
    sys.exit(1)


def main():
    deps_map = get_dependencies()
    # Generate TARGETS file for buck
    ok = generate_targets(get_rocksdb_path(), deps_map)
    if not ok:
        exit_with_error("Failed to generate TARGETS files")

if __name__ == "__main__":
    main()
