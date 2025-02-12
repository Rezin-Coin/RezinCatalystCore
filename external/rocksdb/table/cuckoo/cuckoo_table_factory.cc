// Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#ifndef ROCKSDB_LITE
#include "table/cuckoo/cuckoo_table_factory.h"

#include "db/dbformat.h"
#include "table/cuckoo/cuckoo_table_builder.h"
#include "table/cuckoo/cuckoo_table_reader.h"

namespace rocksdb {

Status CuckooTableFactory::NewTableReader(
    const TableReaderOptions& table_reader_options,
    std::unique_ptr<RandomAccessFileReader>&& file, uint64_t file_size,
    std::unique_ptr<TableReader>* table,
    bool /*prefetch_index_and_filter_in_cache*/) const {
  std::unique_ptr<CuckooTableReader> new_reader(new CuckooTableReader(
      table_reader_options.ioptions, std::move(file), file_size,
      table_reader_options.internal_comparator.user_comparator(), nullptr));
  Status s = new_reader->status();
  if (s.ok()) {
    *table = std::move(new_reader);
  }
  return s;
}

TableBuilder* CuckooTableFactory::NewTableBuilder(
    const TableBuilderOptions& table_builder_options, uint32_t column_family_id,
    WritableFileWriter* file) const {
  // Ignore the skipFIlters flag. Does not apply to this file format
  //

  // TODO: change builder to take the option struct
  return new CuckooTableBuilder(
      file, table_options_.hash_table_ratio, 64,
      table_options_.max_search_depth,
      table_builder_options.internal_comparator.user_comparator(),
      table_options_.cuckoo_block_size, table_options_.use_module_hash,
      table_options_.identity_as_first_hash, nullptr /* get_slice_hash */,
      column_family_id, table_builder_options.column_family_name);
}

std::string CuckooTableFactory::GetPrintableTableOptions() const {
  std::string ret;
  ret.reserve(2000);
  const int kBufferSize = 200;
  char buffer[kBufferSize];

  snprintf(buffer, kBufferSize, "  hash_table_ratio: %lf\n",
           table_options_.hash_table_ratio);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  max_search_depth: %u\n",
           table_options_.max_search_depth);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  cuckoo_block_size: %u\n",
           table_options_.cuckoo_block_size);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  identity_as_first_hash: %d\n",
           table_options_.identity_as_first_hash);
  ret.append(buffer);
  return ret;
}

TableFactory* NewCuckooTableFactory(const CuckooTableOptions& table_options) {
  return new CuckooTableFactory(table_options);
}

}  // namespace rocksdb
#endif  // ROCKSDB_LITE
