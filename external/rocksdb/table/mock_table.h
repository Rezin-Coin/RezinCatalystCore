//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
#pragma once

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "port/port.h"
#include "rocksdb/comparator.h"
#include "rocksdb/table.h"
#include "table/internal_iterator.h"
#include "table/table_builder.h"
#include "table/table_reader.h"
#include "test_util/testharness.h"
#include "test_util/testutil.h"
#include "util/kv_map.h"
#include "util/mutexlock.h"

namespace rocksdb {
namespace mock {

stl_wrappers::KVMap MakeMockFile(
    std::initializer_list<std::pair<const std::string, std::string>> l = {});
stl_wrappers::KVMap MakeMockFile(
    std::vector<std::pair<const std::string, std::string>> l);

struct MockTableFileSystem {
  port::Mutex mutex;
  std::map<uint32_t, stl_wrappers::KVMap> files;
};

class MockTableReader : public TableReader {
 public:
  explicit MockTableReader(const stl_wrappers::KVMap& table) : table_(table) {}

  InternalIterator* NewIterator(const ReadOptions&,
                                const SliceTransform* prefix_extractor,
                                Arena* arena, bool skip_filters,
                                TableReaderCaller caller,
                              size_t compaction_readahead_size = 0) override;

  Status Get(const ReadOptions& readOptions, const Slice& key,
             GetContext* get_context, const SliceTransform* prefix_extractor,
             bool skip_filters = false) override;

  uint64_t ApproximateOffsetOf(const Slice& /*key*/,
                               TableReaderCaller /*caller*/) override {
    return 0;
  }

  uint64_t ApproximateSize(const Slice& /*start*/, const Slice& /*end*/,
                           TableReaderCaller /*caller*/) override {
    return 0;
  }

  size_t ApproximateMemoryUsage() const override { return 0; }

  void SetupForCompaction() override {}

  std::shared_ptr<const TableProperties> GetTableProperties() const override;

  ~MockTableReader() {}

 private:
  const stl_wrappers::KVMap& table_;
};

class MockTableIterator : public InternalIterator {
 public:
  explicit MockTableIterator(const stl_wrappers::KVMap& table) : table_(table) {
    itr_ = table_.end();
  }

  bool Valid() const override { return itr_ != table_.end(); }

  void SeekToFirst() override { itr_ = table_.begin(); }

  void SeekToLast() override {
    itr_ = table_.end();
    --itr_;
  }

  void Seek(const Slice& target) override {
    std::string str_target(target.data(), target.size());
    itr_ = table_.lower_bound(str_target);
  }

  void SeekForPrev(const Slice& target) override {
    std::string str_target(target.data(), target.size());
    itr_ = table_.upper_bound(str_target);
    Prev();
  }

  void Next() override { ++itr_; }

  void Prev() override {
    if (itr_ == table_.begin()) {
      itr_ = table_.end();
    } else {
      --itr_;
    }
  }

  Slice key() const override { return Slice(itr_->first); }

  Slice value() const override { return Slice(itr_->second); }

  Status status() const override { return Status::OK(); }

 private:
  const stl_wrappers::KVMap& table_;
  stl_wrappers::KVMap::const_iterator itr_;
};

class MockTableBuilder : public TableBuilder {
 public:
  MockTableBuilder(uint32_t id, MockTableFileSystem* file_system)
      : id_(id), file_system_(file_system) {
    table_ = MakeMockFile({});
  }

  // REQUIRES: Either Finish() or Abandon() has been called.
  ~MockTableBuilder() {}

  // Add key,value to the table being constructed.
  // REQUIRES: key is after any previously added key according to comparator.
  // REQUIRES: Finish(), Abandon() have not been called
  void Add(const Slice& key, const Slice& value) override {
    table_.insert({key.ToString(), value.ToString()});
  }

  // Return non-ok iff some error has been detected.
  Status status() const override { return Status::OK(); }

  Status Finish() override {
    MutexLock lock_guard(&file_system_->mutex);
    file_system_->files.insert({id_, table_});
    return Status::OK();
  }

  void Abandon() override {}

  uint64_t NumEntries() const override { return table_.size(); }

  uint64_t FileSize() const override { return table_.size(); }

  TableProperties GetTableProperties() const override {
    return TableProperties();
  }

 private:
  uint32_t id_;
  MockTableFileSystem* file_system_;
  stl_wrappers::KVMap table_;
};

class MockTableFactory : public TableFactory {
 public:
  MockTableFactory();
  const char* Name() const override { return "MockTable"; }
  Status NewTableReader(
      const TableReaderOptions& table_reader_options,
      std::unique_ptr<RandomAccessFileReader>&& file, uint64_t file_size,
      std::unique_ptr<TableReader>* table_reader,
      bool prefetch_index_and_filter_in_cache = true) const override;
  TableBuilder* NewTableBuilder(
      const TableBuilderOptions& table_builder_options,
      uint32_t column_familly_id, WritableFileWriter* file) const override;

  // This function will directly create mock table instead of going through
  // MockTableBuilder. file_contents has to have a format of <internal_key,
  // value>. Those key-value pairs will then be inserted into the mock table.
  Status CreateMockTable(Env* env, const std::string& fname,
                         stl_wrappers::KVMap file_contents);

  virtual Status SanitizeOptions(
      const DBOptions& /*db_opts*/,
      const ColumnFamilyOptions& /*cf_opts*/) const override {
    return Status::OK();
  }

  virtual std::string GetPrintableTableOptions() const override {
    return std::string();
  }

  // This function will assert that only a single file exists and that the
  // contents are equal to file_contents
  void AssertSingleFile(const stl_wrappers::KVMap& file_contents);
  void AssertLatestFile(const stl_wrappers::KVMap& file_contents);
  stl_wrappers::KVMap output() {
    assert(!file_system_.files.empty());
    auto latest = file_system_.files.end();
    --latest;
    return latest->second;
  }

 private:
  uint32_t GetAndWriteNextID(WritableFileWriter* file) const;
  uint32_t GetIDFromFile(RandomAccessFileReader* file) const;

  mutable MockTableFileSystem file_system_;
  mutable std::atomic<uint32_t> next_id_;
};

}  // namespace mock
}  // namespace rocksdb
