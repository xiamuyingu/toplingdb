//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "trace_replay/io_tracer.h"

#include "rocksdb/env.h"
#include "rocksdb/status.h"
#include "test_util/testharness.h"
#include "test_util/testutil.h"

namespace ROCKSDB_NAMESPACE {

namespace {
const std::string kDummyFile = "/dummy/file";

}  // namespace

class IOTracerTest : public testing::Test {
 public:
  IOTracerTest() {
    test_path_ = test::PerThreadDBPath("io_tracer_test");
    env_ = ROCKSDB_NAMESPACE::Env::Default();
    EXPECT_OK(env_->CreateDir(test_path_));
    trace_file_path_ = test_path_ + "/io_trace";
  }

  ~IOTracerTest() override {
    EXPECT_OK(env_->DeleteFile(trace_file_path_));
    EXPECT_OK(env_->DeleteDir(test_path_));
  }

  std::string GetFileOperation(uint32_t id) {
    id = id % 4;
    switch (id) {
      case 0:
        return "CreateDir";
      case 1:
        return "GetChildren";
      case 2:
        return "FileSize";
      case 3:
        return "DeleteDir";
    }
    assert(false);
  }

  void WriteIOOp(IOTraceWriter* writer, uint32_t nrecords) {
    assert(writer);
    for (uint32_t i = 0; i < nrecords; i++) {
      IOTraceRecord record;
      record.file_operation = GetFileOperation(i);
      record.io_status = IOStatus::OK().ToString();
      record.file_name = kDummyFile + std::to_string(i);
      record.len = i;
      record.offset = i + 20;
      ASSERT_OK(writer->WriteIOOp(record));
    }
  }

  IOTraceRecord GenerateAccessRecord(int i) {
    IOTraceRecord record;
    record.file_operation = GetFileOperation(i);
    record.io_status = IOStatus::OK().ToString();
    record.file_name = kDummyFile + std::to_string(i);
    record.len = i;
    record.offset = i + 20;
    return record;
  }

  void VerifyIOOp(IOTraceReader* reader, uint32_t nrecords) {
    assert(reader);
    for (uint32_t i = 0; i < nrecords; i++) {
      IOTraceRecord record;
      ASSERT_OK(reader->ReadIOOp(&record));
      ASSERT_EQ(record.file_operation, GetFileOperation(i));
      ASSERT_EQ(record.io_status, IOStatus::OK().ToString());
      ASSERT_EQ(record.len, i);
      ASSERT_EQ(record.offset, i + 20);
      ASSERT_EQ(record.file_name, kDummyFile + std::to_string(i));
    }
  }

  Env* env_;
  EnvOptions env_options_;
  std::string trace_file_path_;
  std::string test_path_;
};

TEST_F(IOTracerTest, AtomicWrite) {
  IOTraceRecord record = GenerateAccessRecord(0);
  {
    TraceOptions trace_opt;
    std::unique_ptr<TraceWriter> trace_writer;
    ASSERT_OK(NewFileTraceWriter(env_, env_options_, trace_file_path_,
                                 &trace_writer));
    IOTracer writer;
    ASSERT_OK(writer.StartIOTrace(env_, trace_opt, std::move(trace_writer)));
    ASSERT_OK(writer.WriteIOOp(record));
    ASSERT_OK(env_->FileExists(trace_file_path_));
  }
  {
    // Verify trace file contains one record.
    std::unique_ptr<TraceReader> trace_reader;
    ASSERT_OK(NewFileTraceReader(env_, env_options_, trace_file_path_,
                                 &trace_reader));
    IOTraceReader reader(std::move(trace_reader));
    IOTraceHeader header;
    ASSERT_OK(reader.ReadHeader(&header));
    ASSERT_EQ(kMajorVersion, header.rocksdb_major_version);
    ASSERT_EQ(kMinorVersion, header.rocksdb_minor_version);
    VerifyIOOp(&reader, 1);
    ASSERT_NOK(reader.ReadIOOp(&record));
  }
}

TEST_F(IOTracerTest, AtomicWriteBeforeStartTrace) {
  IOTraceRecord record = GenerateAccessRecord(0);
  {
    std::unique_ptr<TraceWriter> trace_writer;
    ASSERT_OK(NewFileTraceWriter(env_, env_options_, trace_file_path_,
                                 &trace_writer));
    IOTracer writer;
    // The record should not be written to the trace_file since StartIOTrace is
    // not called.
    ASSERT_OK(writer.WriteIOOp(record));
    ASSERT_OK(env_->FileExists(trace_file_path_));
  }
  {
    // Verify trace file contains nothing.
    std::unique_ptr<TraceReader> trace_reader;
    ASSERT_OK(NewFileTraceReader(env_, env_options_, trace_file_path_,
                                 &trace_reader));
    IOTraceReader reader(std::move(trace_reader));
    IOTraceHeader header;
    ASSERT_NOK(reader.ReadHeader(&header));
  }
}

TEST_F(IOTracerTest, AtomicNoWriteAfterEndTrace) {
  IOTraceRecord record = GenerateAccessRecord(0);
  {
    TraceOptions trace_opt;
    std::unique_ptr<TraceWriter> trace_writer;
    ASSERT_OK(NewFileTraceWriter(env_, env_options_, trace_file_path_,
                                 &trace_writer));
    IOTracer writer;
    ASSERT_OK(writer.StartIOTrace(env_, trace_opt, std::move(trace_writer)));
    ASSERT_OK(writer.WriteIOOp(record));
    writer.EndIOTrace();
    // Write the record again. This time the record should not be written since
    // EndIOTrace is called.
    ASSERT_OK(writer.WriteIOOp(record));
    ASSERT_OK(env_->FileExists(trace_file_path_));
  }
  {
    // Verify trace file contains one record.
    std::unique_ptr<TraceReader> trace_reader;
    ASSERT_OK(NewFileTraceReader(env_, env_options_, trace_file_path_,
                                 &trace_reader));
    IOTraceReader reader(std::move(trace_reader));
    IOTraceHeader header;
    ASSERT_OK(reader.ReadHeader(&header));
    ASSERT_EQ(kMajorVersion, header.rocksdb_major_version);
    ASSERT_EQ(kMinorVersion, header.rocksdb_minor_version);
    VerifyIOOp(&reader, 1);
    ASSERT_NOK(reader.ReadIOOp(&record));
  }
}

TEST_F(IOTracerTest, AtomicMultipleWrites) {
  {
    TraceOptions trace_opt;
    std::unique_ptr<TraceWriter> trace_writer;
    ASSERT_OK(NewFileTraceWriter(env_, env_options_, trace_file_path_,
                                 &trace_writer));
    IOTraceWriter writer(env_, trace_opt, std::move(trace_writer));
    ASSERT_OK(writer.WriteHeader());
    // Write 10 records
    WriteIOOp(&writer, 10);
    ASSERT_OK(env_->FileExists(trace_file_path_));
  }

  {
    // Verify trace file is generated correctly.
    std::unique_ptr<TraceReader> trace_reader;
    ASSERT_OK(NewFileTraceReader(env_, env_options_, trace_file_path_,
                                 &trace_reader));
    IOTraceReader reader(std::move(trace_reader));
    IOTraceHeader header;
    ASSERT_OK(reader.ReadHeader(&header));
    ASSERT_EQ(kMajorVersion, header.rocksdb_major_version);
    ASSERT_EQ(kMinorVersion, header.rocksdb_minor_version);
    // Read 10 records.
    VerifyIOOp(&reader, 10);
    // Read one more and record and it should report error.
    IOTraceRecord record;
    ASSERT_NOK(reader.ReadIOOp(&record));
  }
}
}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
