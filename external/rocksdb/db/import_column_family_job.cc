#ifndef ROCKSDB_LITE

#include "db/import_column_family_job.h"

#include <cinttypes>
#include <algorithm>
#include <string>
#include <vector>

#include "db/version_edit.h"
#include "file/file_util.h"
#include "table/merging_iterator.h"
#include "table/scoped_arena_iterator.h"
#include "table/sst_file_writer_collectors.h"
#include "table/table_builder.h"
#include "util/file_reader_writer.h"
#include "util/stop_watch.h"

namespace rocksdb {

Status ImportColumnFamilyJob::Prepare(uint64_t next_file_number,
                                      SuperVersion* sv) {
  Status status;

  // Read the information of files we are importing
  for (const auto& file_metadata : metadata_) {
    const auto file_path = file_metadata.db_path + "/" + file_metadata.name;
    IngestedFileInfo file_to_import;
    status = GetIngestedFileInfo(file_path, &file_to_import, sv);
    if (!status.ok()) {
      return status;
    }
    files_to_import_.push_back(file_to_import);
  }

  const auto ucmp = cfd_->internal_comparator().user_comparator();
  auto num_files = files_to_import_.size();
  if (num_files == 0) {
    return Status::InvalidArgument("The list of files is empty");
  } else if (num_files > 1) {
    // Verify that passed files don't have overlapping ranges in any particular
    // level.
    int min_level = 1;  // Check for overlaps in Level 1 and above.
    int max_level = -1;
    for (const auto& file_metadata : metadata_) {
      if (file_metadata.level > max_level) {
        max_level = file_metadata.level;
      }
    }
    for (int level = min_level; level <= max_level; ++level) {
      autovector<const IngestedFileInfo*> sorted_files;
      for (size_t i = 0; i < num_files; i++) {
        if (metadata_[i].level == level) {
          sorted_files.push_back(&files_to_import_[i]);
        }
      }

      std::sort(sorted_files.begin(), sorted_files.end(),
                [&ucmp](const IngestedFileInfo* info1,
                        const IngestedFileInfo* info2) {
                  return sstableKeyCompare(ucmp, info1->smallest_internal_key,
                                           info2->smallest_internal_key) < 0;
                });

      for (size_t i = 0; i < sorted_files.size() - 1; i++) {
        if (sstableKeyCompare(ucmp, sorted_files[i]->largest_internal_key,
                              sorted_files[i + 1]->smallest_internal_key) >= 0) {
          return Status::InvalidArgument("Files have overlapping ranges");
        }
      }
    }
  }

  for (const auto& f : files_to_import_) {
    if (f.num_entries == 0) {
      return Status::InvalidArgument("File contain no entries");
    }

    if (!f.smallest_internal_key.Valid() ||
        !f.largest_internal_key.Valid()) {
      return Status::Corruption("File has corrupted keys");
    }
  }

  // Copy/Move external files into DB
  auto hardlink_files = import_options_.move_files;
  for (auto& f : files_to_import_) {
    f.fd = FileDescriptor(next_file_number++, 0, f.file_size);

    const auto path_outside_db = f.external_file_path;
    const auto path_inside_db = TableFileName(
        cfd_->ioptions()->cf_paths, f.fd.GetNumber(), f.fd.GetPathId());

    if (hardlink_files) {
      status = env_->LinkFile(path_outside_db, path_inside_db);
      if (status.IsNotSupported()) {
        // Original file is on a different FS, use copy instead of hard linking
        hardlink_files = false;
      }
    }
    if (!hardlink_files) {
      status = CopyFile(env_, path_outside_db, path_inside_db, 0,
                        db_options_.use_fsync);
    }
    if (!status.ok()) {
      break;
    }
    f.copy_file = !hardlink_files;
    f.internal_file_path = path_inside_db;
  }

  if (!status.ok()) {
    // We failed, remove all files that we copied into the db
    for (const auto& f : files_to_import_) {
      if (f.internal_file_path.empty()) {
        break;
      }
      const auto s = env_->DeleteFile(f.internal_file_path);
      if (!s.ok()) {
        ROCKS_LOG_WARN(db_options_.info_log,
                       "AddFile() clean up for file %s failed : %s",
                       f.internal_file_path.c_str(), s.ToString().c_str());
      }
    }
  }

  return status;
}

// REQUIRES: we have become the only writer by entering both write_thread_ and
// nonmem_write_thread_
Status ImportColumnFamilyJob::Run() {
  Status status;
  edit_.SetColumnFamily(cfd_->GetID());

  for (size_t i = 0; i < files_to_import_.size(); ++i) {
    const auto& f = files_to_import_[i];
    const auto& file_metadata = metadata_[i];
    edit_.AddFile(file_metadata.level, f.fd.GetNumber(), f.fd.GetPathId(),
                  f.fd.GetFileSize(), f.smallest_internal_key,
                  f.largest_internal_key, file_metadata.smallest_seqno,
                  file_metadata.largest_seqno, false);

    // If incoming sequence number is higher, update local sequence number.
    if (file_metadata.largest_seqno > versions_->LastSequence()) {
      versions_->SetLastAllocatedSequence(file_metadata.largest_seqno);
      versions_->SetLastPublishedSequence(file_metadata.largest_seqno);
      versions_->SetLastSequence(file_metadata.largest_seqno);
    }
  }

  return status;
}

void ImportColumnFamilyJob::Cleanup(const Status& status) {
  if (!status.ok()) {
    // We failed to add files to the database remove all the files we copied.
    for (const auto& f : files_to_import_) {
      const auto s = env_->DeleteFile(f.internal_file_path);
      if (!s.ok()) {
        ROCKS_LOG_WARN(db_options_.info_log,
                       "AddFile() clean up for file %s failed : %s",
                       f.internal_file_path.c_str(), s.ToString().c_str());
      }
    }
  } else if (status.ok() && import_options_.move_files) {
    // The files were moved and added successfully, remove original file links
    for (IngestedFileInfo& f : files_to_import_) {
      const auto s = env_->DeleteFile(f.external_file_path);
      if (!s.ok()) {
        ROCKS_LOG_WARN(
            db_options_.info_log,
            "%s was added to DB successfully but failed to remove original "
            "file link : %s",
            f.external_file_path.c_str(), s.ToString().c_str());
      }
    }
  }
}

Status ImportColumnFamilyJob::GetIngestedFileInfo(
    const std::string& external_file, IngestedFileInfo* file_to_import,
    SuperVersion* sv) {
  file_to_import->external_file_path = external_file;

  // Get external file size
  auto status = env_->GetFileSize(external_file, &file_to_import->file_size);
  if (!status.ok()) {
    return status;
  }

  // Create TableReader for external file
  std::unique_ptr<TableReader> table_reader;
  std::unique_ptr<RandomAccessFile> sst_file;
  std::unique_ptr<RandomAccessFileReader> sst_file_reader;

  status = env_->NewRandomAccessFile(external_file, &sst_file, env_options_);
  if (!status.ok()) {
    return status;
  }
  sst_file_reader.reset(new RandomAccessFileReader(std::move(sst_file),
                                                   external_file));

  status = cfd_->ioptions()->table_factory->NewTableReader(
      TableReaderOptions(*cfd_->ioptions(),
                         sv->mutable_cf_options.prefix_extractor.get(),
                         env_options_, cfd_->internal_comparator()),
      std::move(sst_file_reader), file_to_import->file_size, &table_reader);
  if (!status.ok()) {
    return status;
  }

  // Get the external file properties
  auto props = table_reader->GetTableProperties();

  // Set original_seqno to 0.
  file_to_import->original_seqno = 0;

  // Get number of entries in table
  file_to_import->num_entries = props->num_entries;

  ParsedInternalKey key;
  ReadOptions ro;
  // During reading the external file we can cache blocks that we read into
  // the block cache, if we later change the global seqno of this file, we will
  // have block in cache that will include keys with wrong seqno.
  // We need to disable fill_cache so that we read from the file without
  // updating the block cache.
  ro.fill_cache = false;
  std::unique_ptr<InternalIterator> iter(table_reader->NewIterator(
      ro, sv->mutable_cf_options.prefix_extractor.get(), /*arena=*/nullptr,
      /*skip_filters=*/false, TableReaderCaller::kExternalSSTIngestion));

  // Get first (smallest) key from file
  iter->SeekToFirst();
  if (!ParseInternalKey(iter->key(), &key)) {
    return Status::Corruption("external file have corrupted keys");
  }
  file_to_import->smallest_internal_key.SetFrom(key);

  // Get last (largest) key from file
  iter->SeekToLast();
  if (!ParseInternalKey(iter->key(), &key)) {
    return Status::Corruption("external file have corrupted keys");
  }
  file_to_import->largest_internal_key.SetFrom(key);

  file_to_import->cf_id = static_cast<uint32_t>(props->column_family_id);

  file_to_import->table_properties = *props;

  return status;
}

}  // namespace rocksdb

#endif  // !ROCKSDB_LITE
