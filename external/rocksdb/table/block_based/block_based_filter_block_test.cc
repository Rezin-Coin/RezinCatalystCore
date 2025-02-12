//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "table/block_based/block_based_filter_block.h"

#include "rocksdb/filter_policy.h"
#include "table/block_based/block_based_table_reader.h"
#include "test_util/testharness.h"
#include "test_util/testutil.h"
#include "util/coding.h"
#include "util/hash.h"
#include "util/string_util.h"

namespace rocksdb {

// For testing: emit an array with one hash value per key
class TestHashFilter : public FilterPolicy {
 public:
  const char* Name() const override { return "TestHashFilter"; }

  void CreateFilter(const Slice* keys, int n, std::string* dst) const override {
    for (int i = 0; i < n; i++) {
      uint32_t h = Hash(keys[i].data(), keys[i].size(), 1);
      PutFixed32(dst, h);
    }
  }

  bool KeyMayMatch(const Slice& key, const Slice& filter) const override {
    uint32_t h = Hash(key.data(), key.size(), 1);
    for (unsigned int i = 0; i + 4 <= filter.size(); i += 4) {
      if (h == DecodeFixed32(filter.data() + i)) {
        return true;
      }
    }
    return false;
  }
};

class MockBlockBasedTable : public BlockBasedTable {
 public:
  explicit MockBlockBasedTable(Rep* rep)
      : BlockBasedTable(rep, nullptr /* block_cache_tracer */) {}
};

class FilterBlockTest : public testing::Test {
 public:
  Options options_;
  ImmutableCFOptions ioptions_;
  EnvOptions env_options_;
  BlockBasedTableOptions table_options_;
  InternalKeyComparator icomp_;
  std::unique_ptr<BlockBasedTable> table_;

  FilterBlockTest()
      : ioptions_(options_),
        env_options_(options_),
        icomp_(options_.comparator) {
    table_options_.filter_policy.reset(new TestHashFilter);

    constexpr bool skip_filters = false;
    constexpr int level = 0;
    constexpr bool immortal_table = false;
    table_.reset(new MockBlockBasedTable(
        new BlockBasedTable::Rep(ioptions_, env_options_, table_options_,
                                 icomp_, skip_filters, level, immortal_table)));
  }
};

TEST_F(FilterBlockTest, EmptyBuilder) {
  BlockBasedFilterBlockBuilder builder(nullptr, table_options_);
  Slice slice(builder.Finish());
  ASSERT_EQ("\\x00\\x00\\x00\\x00\\x0b", EscapeString(slice));

  CachableEntry<BlockContents> block(
      new BlockContents(slice), nullptr /* cache */, nullptr /* cache_handle */,
      true /* own_value */);

  BlockBasedFilterBlockReader reader(table_.get(), std::move(block));
  ASSERT_TRUE(reader.KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/uint64_t{0},
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader.KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/100000,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
}

TEST_F(FilterBlockTest, SingleChunk) {
  BlockBasedFilterBlockBuilder builder(nullptr, table_options_);
  ASSERT_EQ(0, builder.NumAdded());
  builder.StartBlock(100);
  builder.Add("foo");
  builder.Add("bar");
  builder.Add("box");
  builder.StartBlock(200);
  builder.Add("box");
  builder.StartBlock(300);
  builder.Add("hello");
  ASSERT_EQ(5, builder.NumAdded());
  Slice slice(builder.Finish());

  CachableEntry<BlockContents> block(
      new BlockContents(slice), nullptr /* cache */, nullptr /* cache_handle */,
      true /* own_value */);

  BlockBasedFilterBlockReader reader(table_.get(), std::move(block));
  ASSERT_TRUE(reader.KeyMayMatch("foo", /*prefix_extractor=*/nullptr,
                                 /*block_offset=*/100,
                                 /*no_io=*/false, /*const_ikey_ptr=*/nullptr,
                                 /*get_context=*/nullptr,
                                 /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader.KeyMayMatch("bar", /*prefix_extractor=*/nullptr,
                                 /*block_offset=*/100,
                                 /*no_io=*/false, /*const_ikey_ptr=*/nullptr,
                                 /*get_context=*/nullptr,
                                 /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader.KeyMayMatch("box", /*prefix_extractor=*/nullptr,
                                 /*block_offset=*/100,
                                 /*no_io=*/false, /*const_ikey_ptr=*/nullptr,
                                 /*get_context=*/nullptr,
                                 /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader.KeyMayMatch("hello", /*prefix_extractor=*/nullptr,
                                 /*block_offset=*/100,
                                 /*no_io=*/false, /*const_ikey_ptr=*/nullptr,
                                 /*get_context=*/nullptr,
                                 /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader.KeyMayMatch("foo", /*prefix_extractor=*/nullptr,
                                 /*block_offset=*/100,
                                 /*no_io=*/false, /*const_ikey_ptr=*/nullptr,
                                 /*get_context=*/nullptr,
                                 /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "missing", /*prefix_extractor=*/nullptr, /*block_offset=*/100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "other", /*prefix_extractor=*/nullptr, /*block_offset=*/100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
}

TEST_F(FilterBlockTest, MultiChunk) {
  BlockBasedFilterBlockBuilder builder(nullptr, table_options_);

  // First filter
  builder.StartBlock(0);
  builder.Add("foo");
  builder.StartBlock(2000);
  builder.Add("bar");

  // Second filter
  builder.StartBlock(3100);
  builder.Add("box");

  // Third filter is empty

  // Last filter
  builder.StartBlock(9000);
  builder.Add("box");
  builder.Add("hello");

  Slice slice(builder.Finish());

  CachableEntry<BlockContents> block(
      new BlockContents(slice), nullptr /* cache */, nullptr /* cache_handle */,
      true /* own_value */);

  BlockBasedFilterBlockReader reader(table_.get(), std::move(block));

  // Check first filter
  ASSERT_TRUE(reader.KeyMayMatch("foo", /*prefix_extractor=*/nullptr,
                                 /*block_offset=*/uint64_t{0},
                                 /*no_io=*/false, /*const_ikey_ptr=*/nullptr,
                                 /*get_context=*/nullptr,
                                 /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader.KeyMayMatch("bar", /*prefix_extractor=*/nullptr,
                                 /*block_offset=*/2000,
                                 /*no_io=*/false, /*const_ikey_ptr=*/nullptr,
                                 /*get_context=*/nullptr,
                                 /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "box", /*prefix_extractor=*/nullptr, /*block_offset=*/uint64_t{0},
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "hello", /*prefix_extractor=*/nullptr, /*block_offset=*/uint64_t{0},
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));

  // Check second filter
  ASSERT_TRUE(reader.KeyMayMatch("box", /*prefix_extractor=*/nullptr,
                                 /*block_offset=*/3100,
                                 /*no_io=*/false, /*const_ikey_ptr=*/nullptr,
                                 /*get_context=*/nullptr,
                                 /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/3100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "bar", /*prefix_extractor=*/nullptr, /*block_offset=*/3100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "hello", /*prefix_extractor=*/nullptr, /*block_offset=*/3100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));

  // Check third filter (empty)
  ASSERT_TRUE(!reader.KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/4100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "bar", /*prefix_extractor=*/nullptr, /*block_offset=*/4100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "box", /*prefix_extractor=*/nullptr, /*block_offset=*/4100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "hello", /*prefix_extractor=*/nullptr, /*block_offset=*/4100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));

  // Check last filter
  ASSERT_TRUE(reader.KeyMayMatch("box", /*prefix_extractor=*/nullptr,
                                 /*block_offset=*/9000,
                                 /*no_io=*/false, /*const_ikey_ptr=*/nullptr,
                                 /*get_context=*/nullptr,
                                 /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader.KeyMayMatch("hello", /*prefix_extractor=*/nullptr,
                                 /*block_offset=*/9000,
                                 /*no_io=*/false, /*const_ikey_ptr=*/nullptr,
                                 /*get_context=*/nullptr,
                                 /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/9000,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader.KeyMayMatch(
      "bar", /*prefix_extractor=*/nullptr, /*block_offset=*/9000,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
}

// Test for block based filter block
// use new interface in FilterPolicy to create filter builder/reader
class BlockBasedFilterBlockTest : public testing::Test {
 public:
  Options options_;
  ImmutableCFOptions ioptions_;
  EnvOptions env_options_;
  BlockBasedTableOptions table_options_;
  InternalKeyComparator icomp_;
  std::unique_ptr<BlockBasedTable> table_;

  BlockBasedFilterBlockTest()
      : ioptions_(options_),
        env_options_(options_),
        icomp_(options_.comparator) {
    table_options_.filter_policy.reset(NewBloomFilterPolicy(10));

    constexpr bool skip_filters = false;
    constexpr int level = 0;
    constexpr bool immortal_table = false;
    table_.reset(new MockBlockBasedTable(
        new BlockBasedTable::Rep(ioptions_, env_options_, table_options_,
                                 icomp_, skip_filters, level, immortal_table)));
  }
};

TEST_F(BlockBasedFilterBlockTest, BlockBasedEmptyBuilder) {
  FilterBlockBuilder* builder =
      new BlockBasedFilterBlockBuilder(nullptr, table_options_);
  Slice slice(builder->Finish());
  ASSERT_EQ("\\x00\\x00\\x00\\x00\\x0b", EscapeString(slice));

  CachableEntry<BlockContents> block(
      new BlockContents(slice), nullptr /* cache */, nullptr /* cache_handle */,
      true /* own_value */);

  FilterBlockReader* reader =
      new BlockBasedFilterBlockReader(table_.get(), std::move(block));
  ASSERT_TRUE(reader->KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/uint64_t{0},
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader->KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/10000,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));

  delete builder;
  delete reader;
}

TEST_F(BlockBasedFilterBlockTest, BlockBasedSingleChunk) {
  FilterBlockBuilder* builder =
      new BlockBasedFilterBlockBuilder(nullptr, table_options_);
  builder->StartBlock(100);
  builder->Add("foo");
  builder->Add("bar");
  builder->Add("box");
  builder->StartBlock(200);
  builder->Add("box");
  builder->StartBlock(300);
  builder->Add("hello");
  Slice slice(builder->Finish());

  CachableEntry<BlockContents> block(
      new BlockContents(slice), nullptr /* cache */, nullptr /* cache_handle */,
      true /* own_value */);

  FilterBlockReader* reader =
      new BlockBasedFilterBlockReader(table_.get(), std::move(block));
  ASSERT_TRUE(reader->KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader->KeyMayMatch(
      "bar", /*prefix_extractor=*/nullptr, /*block_offset=*/100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader->KeyMayMatch(
      "box", /*prefix_extractor=*/nullptr, /*block_offset=*/100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader->KeyMayMatch(
      "hello", /*prefix_extractor=*/nullptr, /*block_offset=*/100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader->KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "missing", /*prefix_extractor=*/nullptr, /*block_offset=*/100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "other", /*prefix_extractor=*/nullptr, /*block_offset=*/100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));

  delete builder;
  delete reader;
}

TEST_F(BlockBasedFilterBlockTest, BlockBasedMultiChunk) {
  FilterBlockBuilder* builder =
      new BlockBasedFilterBlockBuilder(nullptr, table_options_);

  // First filter
  builder->StartBlock(0);
  builder->Add("foo");
  builder->StartBlock(2000);
  builder->Add("bar");

  // Second filter
  builder->StartBlock(3100);
  builder->Add("box");

  // Third filter is empty

  // Last filter
  builder->StartBlock(9000);
  builder->Add("box");
  builder->Add("hello");

  Slice slice(builder->Finish());

  CachableEntry<BlockContents> block(
      new BlockContents(slice), nullptr /* cache */, nullptr /* cache_handle */,
      true /* own_value */);

  FilterBlockReader* reader =
      new BlockBasedFilterBlockReader(table_.get(), std::move(block));

  // Check first filter
  ASSERT_TRUE(reader->KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/uint64_t{0},
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader->KeyMayMatch(
      "bar", /*prefix_extractor=*/nullptr, /*block_offset=*/2000,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "box", /*prefix_extractor=*/nullptr, /*block_offset=*/uint64_t{0},
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "hello", /*prefix_extractor=*/nullptr, /*block_offset=*/uint64_t{0},
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));

  // Check second filter
  ASSERT_TRUE(reader->KeyMayMatch(
      "box", /*prefix_extractor=*/nullptr, /*block_offset=*/3100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/3100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "bar", /*prefix_extractor=*/nullptr, /*block_offset=*/3100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "hello", /*prefix_extractor=*/nullptr, /*block_offset=*/3100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));

  // Check third filter (empty)
  ASSERT_TRUE(!reader->KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/4100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "bar", /*prefix_extractor=*/nullptr, /*block_offset=*/4100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "box", /*prefix_extractor=*/nullptr, /*block_offset=*/4100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "hello", /*prefix_extractor=*/nullptr, /*block_offset=*/4100,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));

  // Check last filter
  ASSERT_TRUE(reader->KeyMayMatch(
      "box", /*prefix_extractor=*/nullptr, /*block_offset=*/9000,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(reader->KeyMayMatch(
      "hello", /*prefix_extractor=*/nullptr, /*block_offset=*/9000,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "foo", /*prefix_extractor=*/nullptr, /*block_offset=*/9000,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));
  ASSERT_TRUE(!reader->KeyMayMatch(
      "bar", /*prefix_extractor=*/nullptr, /*block_offset=*/9000,
      /*no_io=*/false, /*const_ikey_ptr=*/nullptr, /*get_context=*/nullptr,
      /*lookup_context=*/nullptr));

  delete builder;
  delete reader;
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
