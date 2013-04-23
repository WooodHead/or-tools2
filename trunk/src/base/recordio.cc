// Copyright 2010-2012 Google
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <zlib.h>
#include <string>
#include "base/logging.h"
#include "base/recordio.h"
#include "base/scoped_ptr.h"

namespace operations_research {
const int RecordWriter::kMagicNumber = 0x3ed7230a;

RecordWriter::RecordWriter(File* const file)
    : file_(file), use_compression_(true) {}

bool RecordWriter::Close() {
  return file_->Close();
}

void RecordWriter::set_use_compression(bool use_compression) {
  use_compression_ = use_compression;
}

std::string RecordWriter::Compress(std::string const& s) const {
  const unsigned long source_size = s.size();  // NOLINT
  const char * source = s.c_str();

  unsigned long dsize = source_size + (source_size * 0.1f) + 16;  // NOLINT
  scoped_ptr<char> destination(new char[dsize]);
  // Use compress() from zlib.h.
  const int result =
      compress(reinterpret_cast<unsigned char *>(destination.get()), &dsize,
               reinterpret_cast<const unsigned char *>(source), source_size);

  if (result != Z_OK) {
    LOG(FATAL) << "Compress error occured! Error code: " << result;
  }
  return std::string(destination.get(), dsize);
}

RecordReader::RecordReader(File* const file) : file_(file) {}

bool RecordReader::Close() {
  return file_->Close();
}

void RecordReader::Uncompress(const char* const source,
                             uint64 source_size,
                             char* const output_buffer,
                             uint64 output_size) const {
  unsigned long result_size = output_size;  // NOLINT
  // Use uncompress() from zlib.h
  const int result =
      uncompress(reinterpret_cast<unsigned char *>(output_buffer), &result_size,
                 reinterpret_cast<const unsigned char *>(source), source_size);
  if (result != Z_OK) {
    LOG(FATAL) << "Uncompress error occured! Error code: " << result;
  }
  CHECK_LE(result_size, static_cast<unsigned long>(output_size));  // NOLINT
}
}  // namespace operations_research