/** Copyright 2020 Alibaba Group Holding Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GRAPE_UTILS_MMAP_ARRAY_H_
#define GRAPE_UTILS_MMAP_ARRAY_H_

#include <atomic>

#include "grape/types.h"
#include "grape/util.h"

namespace grape {

template <typename T>
class mmap_array {
 public:
  mmap_array()
      : filename_(""),
        fd_(-1),
        data_(NULL),
        mmapped_size_in_bytes_(0),
        size_(0),
        read_only_(false) {}
  mmap_array(mmap_array&& rhs)
      : filename_(rhs.filename_),
        fd_(rhs.fd_),
        data_(rhs.data_),
        mmapped_size_in_bytes_(rhs.mmapped_size_in_bytes_),
        size_(rhs.size_),
        read_only_(rhs.read_only_) {
    rhs.filename_ = "";
    rhs.fd_ = -1;
    rhs.data_ = NULL;
    rhs.mmapped_size_in_bytes_ = 0;
    rhs.size_ = 0;
    rhs.read_only_ = false;
  }

  ~mmap_array() {
    if (data_ != NULL) {
      munmap(data_, mmapped_size_in_bytes_);
    }
    if (fd_ != -1) {
      close(fd_);
    }
  }

  void reset() {
    if (data_ != NULL) {
      munmap(data_, mmapped_size_in_bytes_);
    }
    if (fd_ != -1) {
      close(fd_);
    }
    filename_ = "";
    fd_ = -1;
    data_ = NULL;
    mmapped_size_in_bytes_ = 0;
    size_ = 0;
    read_only_ = false;
  }

  void open(const std::string& filename, bool read_only) {
    reset();
    filename_ = filename;
    read_only_ = read_only;
    if (read_only) {
      if (!exists_file(filename_)) {
        LOG(ERROR) << "file [" << filename_ << "] does not exist";
        return;
      }
      fd_ = ::open(filename_.c_str(), O_RDONLY);
      mmapped_size_in_bytes_ = file_size(filename_);
      size_ = mmapped_size_in_bytes_ / sizeof(T);
      if (size_ == 0) {
        mmapped_size_in_bytes_ = 0;
      } else {
        data_ = reinterpret_cast<T*>(
            mmap(NULL, mmapped_size_in_bytes_, PROT_READ, MAP_PRIVATE, fd_, 0));
      }
    } else {
      fd_ = ::open(filename_.c_str(), O_RDWR | O_CREAT, 0666);
      mmapped_size_in_bytes_ = file_size(filename_);
      size_ = mmapped_size_in_bytes_ / sizeof(T);
      if (size_ == 0) {
        mmapped_size_in_bytes_ = 0;
      } else {
        data_ = reinterpret_cast<T*>(mmap(NULL, mmapped_size_in_bytes_,
                                          PROT_READ | PROT_WRITE, MAP_SHARED,
                                          fd_, 0));
      }
    }
    if (data_ == MAP_FAILED) {
      LOG(ERROR) << "mmap failed";
      data_ = NULL;
      size_ = 0;
      mmapped_size_in_bytes_ = 0;
    }
  }

  void resize(size_t size) {
    if (size == size_) {
      return;
    }
    if (size * sizeof(T) < mmapped_size_in_bytes_) {
      size_ = size;
      return;
    }
    if (fd_ == -1) {
      T* new_data = reinterpret_cast<T*>(
          mmap(NULL, size * sizeof(T), PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
      if (new_data == MAP_FAILED) {
        LOG(ERROR) << "mmap failed";
        return;
      }
      if (size_ != 0) {
        memcpy(new_data, data_, size_ * sizeof(T));
        munmap(data_, mmapped_size_in_bytes_);
      }
      data_ = new_data;
      mmapped_size_in_bytes_ = size * sizeof(T);
      size_ = size;
    } else {
      if (read_only_) {
        LOG(ERROR)
            << "cannot resize read-only mmap_array to larger size than file";
      } else {
        if (data_ != NULL) {
          munmap(data_, mmapped_size_in_bytes_);
        }
        mmapped_size_in_bytes_ = size * sizeof(T);
        size_ = size;
        if (!ftruncate(fd_, mmapped_size_in_bytes_)) {
          LOG(ERROR) << "ftruncate failed: " << filename_;
          perror("truncate");
          reset();
          return;
        }
        if (size_ == 0) {
          data_ = NULL;
        } else {
          data_ = reinterpret_cast<T*>(mmap(NULL, mmapped_size_in_bytes_,
                                            PROT_READ | PROT_WRITE, MAP_SHARED,
                                            fd_, 0));
        }
      }
    }
  }

  bool read_only() const { return read_only_; }
  const std::string& filename() const { return filename_; }
  size_t size() const { return size_; }

  const T& get(size_t idx) const { return data_[idx]; }
  void set(size_t idx, const T& val) { data_[idx] = val; }

  void swap(mmap_array& rhs) {
    std::swap(filename_, rhs.filename_);
    std::swap(fd_, rhs.fd_);
    std::swap(data_, rhs.data_);
    std::swap(mmapped_size_in_bytes_, rhs.mmapped_size_in_bytes_);
    std::swap(size_, rhs.size_);
    std::swap(read_only_, rhs.read_only_);
  }

  void save(const std::string& filename) const {
    FILE* fout = fopen(filename.c_str(), "wb");
    if (fout == NULL) {
      LOG(ERROR) << "open file [" << filename << "] failed";
      return;
    }
    fwrite(data_, sizeof(T), size_, fout);
    fflush(fout);
    fclose(fout);
  }

  const T* data() const { return data_; }
  T* data() { return data_; }

  size_t memory_usage() const { return mmapped_size_in_bytes_; }

 private:
  std::string filename_;
  int fd_;

  T* data_;
  size_t mmapped_size_in_bytes_;
  size_t size_;

  bool read_only_;
};

template <>
class mmap_array<string_view> {
 public:
  struct string_item {
    uint64_t offset : 48;
    uint32_t length : 16;
  };

  mmap_array() : max_length_(128), data_size_(0) {}
  mmap_array(mmap_array&& rhs)
      : items_(std::move(rhs.items_)),
        data_(std::move(rhs.data_)),
        max_length_(rhs.max_length_) {
    data_size_.store(rhs.data_size_.load());
    rhs.data_size_.store(0);
  }
  ~mmap_array() {}

  void set_max_length(int max_length) {
    max_length_ = max_length;
    std::string fn = filename();
    if (!fn.empty()) {
      FILE* fout_meta = fopen((fn + ".meta").c_str(), "wb");
      CHECK_EQ(fwrite(&max_length_, sizeof(int), 1, fout_meta), 1);
      fflush(fout_meta);
      fclose(fout_meta);
    }
  }

  void reset() {
    items_.reset();
    data_.reset();
    max_length_ = 128;
    data_size_.store(0);
  }

  void open(const std::string& filename, bool read_only) {
    reset();
    prefix_ = filename;
    items_.open(filename + ".items", read_only);
    data_.open(filename + ".data", read_only);
    data_size_.store(data_.size());
    if (exists_file(filename + ".meta")) {
      FILE* fin_meta = fopen((filename + ".meta").c_str(), "rb");
      CHECK_EQ(fread(&max_length_, sizeof(int), 1, fin_meta), 1);
      fclose(fin_meta);
    } else {
      FILE* fout_meta = fopen((filename + ".meta").c_str(), "wb");
      CHECK_EQ(fwrite(&max_length_, sizeof(int), 1, fout_meta), 1);
      fflush(fout_meta);
      fclose(fout_meta);
    }
  }

  void save(const std::string& prefix) const {
    items_.save(prefix + ".items");
    data_.save(prefix + ".data");
    {
      FILE* fout_meta = fopen((prefix + ".meta").c_str(), "wb");
      CHECK_EQ(fwrite(&max_length_, sizeof(int), 1, fout_meta), 1);
      fflush(fout_meta);
      fclose(fout_meta);
    }
  }

  void resize(size_t size) {
    size_t old_size = items_.size();
    items_.resize(size);
    size_t new_data_size = (size - old_size) * max_length_ + data_size_.load();
    if (new_data_size > data_.size()) {
      data_.resize(new_data_size);
    }
  }

  void _resize(size_t items_size, size_t data_size) {
    items_.resize(items_size);
    data_.resize(data_size);
  }

  bool read_only() const { return items_.read_only(); }
  const std::string& filename() const { return prefix_; }
  size_t size() const { return items_.size(); }

  string_view get(size_t idx) const {
    const string_item& item = items_.get(idx);
    return string_view(data_.data() + item.offset, item.length);
  }

  void set(size_t idx, const string_view& val) {
    size_t offset = data_size_.fetch_add(val.size());
    items_.set(idx, {offset, static_cast<uint32_t>(val.size())});
    memcpy(data_.data() + offset, val.data(), val.size());
  }

  void _set(size_t item_idx, size_t data_offset, const string_view& val) {
    items_.set(item_idx, {data_offset, static_cast<uint32_t>(val.size())});
    memcpy(data_.data() + data_offset, val.data(), val.size());
  }

  const string_item* items() const { return items_.data(); }
  const char* data() const { return data_.data(); }

  const mmap_array<string_item>& items_array() const { return items_; }
  mmap_array<string_item>& items_array() { return items_; }

  const mmap_array<char>& data_array() const { return data_; }
  mmap_array<char>& data_array() { return data_; }

  int max_length() const { return max_length_; }
  size_t data_size() const { return data_size_.load(); }
  void set_data_size(size_t data_size) { data_size_.store(data_size); }

  size_t memory_usage() const {
    return items_.memory_usage() + data_.memory_usage();
  }

 private:
  std::string prefix_;
  mmap_array<string_item> items_;
  mmap_array<char> data_;
  int max_length_;
  std::atomic<size_t> data_size_;
};

}  // namespace grape

#endif  // GRAPE_UTILS_MMAP_ARRAY_H_