#ifndef GRAPE_UTILS_PHF_IMPL_CONTAINERS_H_
#define GRAPE_UTILS_PHF_IMPL_CONTAINERS_H_

#include <assert.h>
#include <stdint.h>

#include <pthash/encoders/compact_vector.hpp>

namespace grape {

namespace phf_impl {

template <typename T>
struct vector_view {
 public:
  vector_view() : m_data(NULL), m_size(0) {}
  ~vector_view() {}

  size_t bytes() const { return sizeof(m_size) + m_size * sizeof(T); }

  const T& operator[](size_t i) const {
    assert(i < m_size);
    return m_data[i];
  }

  const T* data() const { return m_data; }
  size_t size() const { return m_size; }

  void swap(vector_view& other) {
    std::swap(m_data, other.m_data);
    std::swap(m_size, other.m_size);
  }

 private:
  T* m_data;
  size_t m_size;
};

struct compact_vector_view {
  compact_vector_view() : m_size(0), m_width(0), m_mask(0), m_bits() {}

  template <typename Data>
  struct enumerator {
    enumerator() {}

    enumerator(Data const* data, uint64_t i = 0)
        : m_i(i),
          m_cur_val(0),
          m_cur_block((i * data->m_width) >> 6),
          m_cur_shift((i * data->m_width) & 63),
          m_data(data) {}

    uint64_t operator*() {
      read();
      return m_cur_val;
    }

    enumerator& operator++() {
      ++m_i;
      return *this;
    }

    inline uint64_t value() {
      read();
      return m_cur_val;
    }

    inline void next() { ++m_i; }

    bool operator==(enumerator const& other) const { return m_i == other.m_i; }

    bool operator!=(enumerator const& other) const { return !(*this == other); }

   private:
    uint64_t m_i;
    uint64_t m_cur_val;
    uint64_t m_cur_block;
    int64_t m_cur_shift;
    Data const* m_data;

    void read() {
      if (m_cur_shift + m_data->m_width <= 64) {
        m_cur_val = m_data->m_bits[m_cur_block] >> m_cur_shift & m_data->m_mask;
      } else {
        uint64_t res_shift = 64 - m_cur_shift;
        m_cur_val =
            (m_data->m_bits[m_cur_block] >> m_cur_shift) |
            (m_data->m_bits[m_cur_block + 1] << res_shift & m_data->m_mask);
        ++m_cur_block;
        m_cur_shift = -res_shift;
      }

      m_cur_shift += m_data->m_width;

      if (m_cur_shift == 64) {
        m_cur_shift = 0;
        ++m_cur_block;
      }
    }
  };

  inline uint64_t operator[](uint64_t i) const {
    assert(i < size());
    uint64_t pos = i * m_width;
    uint64_t block = pos >> 6;
    uint64_t shift = pos & 63;
    if (shift + m_width <= 64) {
      return m_bits[block] >> shift & m_mask;
    } else {
      uint64_t res_shift = 64 - shift;
      return (m_bits[block] >> shift) |
             (m_bits[block + 1] << res_shift & m_mask);
    }
  }

  // it retrieves at least 57 bits
  inline uint64_t access(uint64_t pos) const {
    assert(pos < size());
    uint64_t i = pos * m_width;
    const char* ptr = reinterpret_cast<const char*>(m_bits.data());
    return (*(reinterpret_cast<uint64_t const*>(ptr + (i >> 3))) >> (i & 7)) &
           m_mask;
  }

  uint64_t back() const { return operator[](size() - 1); }

  inline uint64_t size() const { return m_size; }

  inline uint64_t width() const { return m_width; }

  typedef enumerator<compact_vector_view> iterator;

  iterator begin() const { return iterator(this); }

  iterator end() const { return iterator(this, size()); }

  iterator at(uint64_t pos) const { return iterator(this, pos); }

  size_t bytes() const {
    return sizeof(m_size) + sizeof(m_width) + sizeof(m_mask) + m_bits.bytes();
  }

  void swap(compact_vector_view& other) {
    std::swap(m_size, other.m_size);
    std::swap(m_width, other.m_width);
    std::swap(m_mask, other.m_mask);
    m_bits.swap(other.m_bits);
  }

  template <typename Visitor>
  void visit(Visitor& visitor) {
    visitor.visit(m_size);
    visitor.visit(m_width);
    visitor.visit(m_mask);
    visitor.visit(m_bits);
  }

 private:
  uint64_t m_size;
  uint64_t m_width;
  uint64_t m_mask;
  vector_view<uint64_t> m_bits;
};

struct bit_vector_view {
  bit_vector_view() : m_size(0) {}

  void swap(bit_vector_view& other) {
    std::swap(m_size, other.m_size);
    m_bits.swap(other.m_bits);
  }

  inline size_t size() const { return m_size; }

  uint64_t bytes() const { return sizeof(m_size) + m_bits.bytes(); }

  inline uint64_t operator[](uint64_t i) const {
    assert(i < size());
    uint64_t block = i >> 6;
    uint64_t shift = i & 63;
    return m_bits[block] >> shift & uint64_t(1);
  }

  inline uint64_t get_bits(uint64_t pos, uint64_t len) const {
    assert(pos + len <= size());
    if (!len)
      return 0;
    uint64_t block = pos >> 6;
    uint64_t shift = pos & 63;
    if (shift + len <= 64) {
      return m_bits[block] >> shift & ((uint64_t(1) << len) - 1);
    } else {
      uint64_t res_shift = 64 - shift;
      return (m_bits[block] >> shift) |
             (m_bits[block + 1] << res_shift & ((uint64_t(1) << len) - 1));
    }
  }

  // fast and unsafe version: it retrieves at least 56 bits
  inline uint64_t get_word56(uint64_t pos) const {
    const char* base_ptr = reinterpret_cast<const char*>(m_bits.data());
    return *(reinterpret_cast<uint64_t const*>(base_ptr + (pos >> 3))) >>
           (pos & 7);
  }

  // pad with zeros if extension further size is needed
  inline uint64_t get_word64(uint64_t pos) const {
    assert(pos < size());
    uint64_t block = pos >> 6;
    uint64_t shift = pos & 63;
    uint64_t word = m_bits[block] >> shift;
    if (shift && block + 1 < m_bits.size()) {
      word |= m_bits[block + 1] << (64 - shift);
    }
    return word;
  }

  inline uint64_t predecessor1(uint64_t pos) const {
    assert(pos < m_size);
    uint64_t block = pos / 64;
    uint64_t shift = 64 - pos % 64 - 1;
    uint64_t word = m_bits[block];
    word = (word << shift) >> shift;

    unsigned long ret;
    while (!pthash::util::msb(word, ret)) {
      assert(block);
      word = m_bits[--block];
    };
    return block * 64 + ret;
  }

  vector_view<uint64_t> const& data() const { return m_bits; }

  struct unary_iterator {
    unary_iterator() : m_data(0), m_position(0), m_buf(0) {}

    unary_iterator(bit_vector_view const& bv, uint64_t pos = 0) {
      m_data = bv.data().data();
      m_position = pos;
      m_buf = m_data[pos >> 6];
      // clear low bits
      m_buf &= uint64_t(-1) << (pos & 63);
    }

    uint64_t position() const { return m_position; }

    uint64_t next() {
      unsigned long pos_in_word;
      uint64_t buf = m_buf;
      while (!pthash::util::lsb(buf, pos_in_word)) {
        m_position += 64;
        buf = m_data[m_position >> 6];
      }

      m_buf = buf & (buf - 1);  // clear LSB
      m_position = (m_position & ~uint64_t(63)) + pos_in_word;
      return m_position;
    }

    // skip to the k-th one after the current position
    void skip(uint64_t k) {
      uint64_t skipped = 0;
      uint64_t buf = m_buf;
      uint64_t w = 0;
      while (skipped + (w = pthash::util::popcount(buf)) <= k) {
        skipped += w;
        m_position += 64;
        buf = m_data[m_position / 64];
      }
      assert(buf);
      uint64_t pos_in_word = pthash::util::select_in_word(buf, k - skipped);
      m_buf = buf & (uint64_t(-1) << pos_in_word);
      m_position = (m_position & ~uint64_t(63)) + pos_in_word;
    }

    // skip to the k-th zero after the current position
    void skip0(uint64_t k) {
      uint64_t skipped = 0;
      uint64_t pos_in_word = m_position % 64;
      uint64_t buf = ~m_buf & (uint64_t(-1) << pos_in_word);
      uint64_t w = 0;
      while (skipped + (w = pthash::util::popcount(buf)) <= k) {
        skipped += w;
        m_position += 64;
        buf = ~m_data[m_position / 64];
      }
      assert(buf);
      pos_in_word = pthash::util::select_in_word(buf, k - skipped);
      m_buf = ~buf & (uint64_t(-1) << pos_in_word);
      m_position = (m_position & ~uint64_t(63)) + pos_in_word;
    }

   private:
    uint64_t const* m_data;
    uint64_t m_position;
    uint64_t m_buf;
  };

  template <typename Visitor>
  void visit(Visitor& visitor) {
    visitor.visit(m_size);
    visitor.visit(m_bits);
  }

 protected:
  size_t m_size;
  vector_view<uint64_t> m_bits;
};

template <typename WordGetter>
struct darray_view {
  darray_view() : m_positions() {}

  void swap(darray& other) {
    std::swap(other.m_positions, m_positions);
    m_block_inventory.swap(other.m_block_inventory);
    m_subblock_inventory.swap(other.m_subblock_inventory);
    m_overflow_positions.swap(other.m_overflow_positions);
  }

  inline uint64_t select(bit_vector_view const& bv, uint64_t idx) const {
    assert(idx < num_positions());
    uint64_t block = idx / block_size;
    int64_t block_pos = m_block_inventory[block];
    if (block_pos < 0) {  // sparse super-block
      uint64_t overflow_pos = uint64_t(-block_pos - 1);
      return m_overflow_positions[overflow_pos + (idx & (block_size - 1))];
    }

    size_t subblock = idx / subblock_size;
    size_t start_pos = uint64_t(block_pos) + m_subblock_inventory[subblock];
    size_t reminder = idx & (subblock_size - 1);
    if (!reminder)
      return start_pos;

    vector_view<uint64_t> const& data = bv.data();
    size_t word_idx = start_pos >> 6;
    size_t word_shift = start_pos & 63;
    uint64_t word = WordGetter()(data, word_idx) & (uint64_t(-1) << word_shift);
    while (true) {
      size_t popcnt = util::popcount(word);
      if (reminder < popcnt)
        break;
      reminder -= popcnt;
      word = WordGetter()(data, ++word_idx);
    }
    return (word_idx << 6) + util::select_in_word(word, reminder);
  }

  inline uint64_t num_positions() const { return m_positions; }

  uint64_t bytes() const {
    return sizeof(m_positions) + m_block_inventory.bytes() +
           m_subblock_inventory.bytes() + m_overflow_positions.bytes();
  }

  template <typename Visitor>
  void visit(Visitor& visitor) {
    visitor.visit(m_positions);
    visitor.visit(m_block_inventory);
    visitor.visit(m_subblock_inventory);
    visitor.visit(m_overflow_positions);
  }

 protected:
  static const size_t block_size = 1024;  // 2048
  static const size_t subblock_size = 32;
  static const size_t max_in_block_distance = 1 << 16;

  size_t m_positions;
  vector_view<int64_t> m_block_inventory;
  vector_view<uint16_t> m_subblock_inventory;
  vector_view<uint64_t> m_overflow_positions;
};

}  // namespace phf_impl

}  // namespace grape

#endif  // GRAPE_UTILS_PHF_IMPL_CONTAINERS_H_