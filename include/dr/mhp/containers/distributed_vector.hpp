// SPDX-FileCopyrightText: Intel Corporation
//
// SPDX-License-Identifier: BSD-3-Clause

namespace mhp {

// It won't have segments if it is a zip of non-aligned data
bool aligned(lib::distributed_iterator auto iter) {
  return !lib::ranges::segments(iter).empty();
}

// iter1 is aligned with iter2, and iter2 is aligned with the rest
bool aligned(lib::distributed_iterator auto iter1,
             lib::distributed_iterator auto iter2,
             lib::distributed_iterator auto... iters) {
  auto combined = rng::views::zip(lib::ranges::segments(iter1),
                                  lib::ranges::segments(iter2));
  if (combined.empty())
    return false;
  for (auto seg : combined) {
    if (lib::ranges::rank(seg.first) != lib::ranges::rank(seg.second) ||
        seg.first.size() != seg.second.size()) {
      return false;
    }
  }

  return aligned(iter2, iters...);
}

// 1D, homogeneous, distributed storage
template <typename T, typename Allocator> struct storage {
public:
  // Cannot copy without transferring ownership of the storage
  storage(const storage &) = delete;
  storage &operator=(const storage &) = delete;

  storage(std::size_t size, lib::span_halo<T> *halo, lib::halo_bounds hb,
          lib::communicator comm, Allocator allocator)
      : segment_size_(segment_size(hb, size, comm)),
        data_size_(segment_size_ + hb.prev + hb.next),
        data_(allocator.allocate(data_size_)) {
    allocator_ = allocator;
    halo_ = halo;
    comm_ = comm;
    halo_bounds_ = hb;
    container_size_ = size;
    container_capacity_ = comm.size() * segment_size_;
    win_.create(comm, data_, data_size_ * sizeof(T));
    fence();
    lib::drlog.debug("Storage allocated\n  {}\n", *this);
  }

  ~storage() {
    lib::drlog.debug("Deleting data_\n");
    fence();
    win_.free();
    allocator_.deallocate(data_, data_size_);
    data_ = nullptr;
  }

  static auto segment_size(auto hb, auto size, auto comm) {
    // make segment as least as big as halo to ensure halo only comes nearest
    // neighbor.
    return std::max({(size + comm.size() - 1) / comm.size(), hb.prev, hb.next});
  }

  T get(std::size_t index) const {
    auto segment = segment_index(index);
    auto local = local_index(index) + halo_bounds_.prev;
    auto val = win_.get<T>(segment, local);
    lib::drlog.debug("get {} =  {} ({}:{})\n", val, index, segment, local);
    return val;
  }

  void put(std::size_t index, const T &val) const {
    auto segment = segment_index(index);
    auto local = local_index(index) + halo_bounds_.prev;
    lib::drlog.debug("put {} ({}:{}) = {}\n", index, segment, local, val);
    win_.put(val, segment, local);
  }

  // Undefined if you are iterating over a segment because the end of
  // segment points to the beginning of the next segment
  auto segment_index(std::size_t index) const { return index / segment_size_; }
  auto local_index(std::size_t index) const { return index % segment_size_; }

  T *local(std::size_t index) const {
    lib::drlog.debug("local: index: {} rank: {}\n", index, rank(index));
    if (rank(index) == std::size_t(comm_.rank())) {
      return data_ + local_index(index) + halo_bounds_.prev;
    } else {
      return nullptr;
    }
  }

  auto rank(std::size_t index) const {
    return segment_index(index) % comm_.size();
  }

  void barrier() const { comm_.barrier(); }
  void fence() const { win_.fence(); }
  auto my_rank() const { return comm_.rank(); }
  void halo_exchange() const { halo_->exchange(); }

  std::size_t container_size_ = 0;
  std::size_t container_capacity_ = 0;

  lib::communicator comm_;
  lib::communicator::win win_;

  // member initializer list requires this order
  lib::halo_bounds halo_bounds_;
  std::size_t segment_size_ = 0;
  std::size_t data_size_ = 0;
  T *data_;
  lib::span_halo<T> *halo_;
  Allocator allocator_;
};

template <typename T, typename Allocator> class distributed_vector_reference;

template <typename T, typename Allocator> class distributed_vector_iterator {
private:
  using reference = distributed_vector_reference<T, Allocator>;
  using iterator = distributed_vector_iterator;
  using storage_type = storage<T, Allocator>;

public:
  // Required for random access iterator
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  distributed_vector_iterator() = default;
  distributed_vector_iterator(const storage_type *storage, std::size_t index)
      : storage_(storage), index_(index) {}

  // Comparison
  bool operator==(const iterator &other) const noexcept {
    return index_ == other.index_ && storage_ == other.storage_;
  }
  auto operator<=>(const iterator &other) const noexcept {
    return index_ <=> other.index_;
  }

  // Only these arithmetics manipulate internal state
  auto &operator-=(difference_type n) {
    index_ -= n;
    return *this;
  }
  auto &operator+=(difference_type n) {
    index_ += n;
    return *this;
  }
  difference_type operator-(const iterator &other) const noexcept {
    return index_ - other.index_;
  }

  // prefix
  auto &operator++() {
    *this += 1;
    return *this;
  }
  auto &operator--() {
    *this -= 1;
    return *this;
  }

  // postfix
  auto operator++(int) {
    auto prev = *this;
    *this += 1;
    return prev;
  }
  auto operator--(int) {
    auto prev = *this;
    *this -= 1;
    return prev;
  }

  auto operator+(difference_type n) const {
    auto p = *this;
    p += n;
    return p;
  }
  auto operator-(difference_type n) const {
    auto p = *this;
    p -= n;
    return p;
  }

  // When *this is not first in the expression
  friend auto operator+(difference_type n, const iterator &other) {
    return other + n;
  }

  // dereference
  reference operator*() const { return reference{*this}; }
  reference operator[](difference_type n) const { return reference{*this + n}; }

  T get() const { return storage_->get(index_); }
  void put(const T &value) const { storage_->put(index_, value); }

  auto rank() const { return storage_->rank(index_); }
  auto local() const { return storage_->local(index_); }

  auto segments() const {
    return lib::internal::drop_segments(
        rng::views::chunk(make_range(), storage_->segment_size_), index_);
  }
  auto segment_index() const { return storage_->segment_index(index_); }
  auto local_index() const { return storage_->local_index(index_); }

  void barrier() const { storage_->barrier(); }
  void fence() const { storage_->fence(); }
  void halo_exchange() const { storage_->halo_exchange(); }
  auto my_rank() const { return storage_->my_rank(); }

public:
  auto make_range() const {
    return rng::subrange(
        distributed_vector_iterator(storage_, 0),
        distributed_vector_iterator(storage_, storage_->container_size_));
  }

  const storage_type *storage_ = nullptr;
  std::size_t index_ = 0;
};

template <typename T, typename Allocator> class distributed_vector_reference {
  using reference = distributed_vector_reference;
  using iterator = distributed_vector_iterator<T, Allocator>;

public:
  distributed_vector_reference(const iterator it) : iterator_(it) {}

  operator T() const { return iterator_.get(); }
  reference operator=(const T &value) const {
    iterator_.put(value);
    return *this;
  }
  reference operator=(const reference &other) const {
    *this = T(other);
    return *this;
  }
  iterator operator&() const { return iterator_; }

private:
  const iterator iterator_;
};

template <typename T, typename Allocator = std::allocator<T>>
struct distributed_vector {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  using iterator = distributed_vector_iterator<T, Allocator>;
  using pointer = iterator;
  using reference = std::iter_reference_t<iterator>;

  distributed_vector() {}

  distributed_vector(std::size_t count,
                     lib::halo_bounds hb = lib::halo_bounds(),
                     Allocator allocator = Allocator())
      : storage_(count, &halo_, hb, lib::communicator(), allocator),
        halo_(storage_.comm_, storage_.data_, storage_.data_size_, hb) {}

  distributed_vector(const distributed_vector &) = delete;
  distributed_vector &operator=(const distributed_vector &) = delete;

  reference operator[](size_type pos) const { return begin()[pos]; }

  size_type size() const noexcept { return storage_.container_size_; }

  auto segments() { return begin().segments(); }

  iterator begin() const { return iterator(&storage_, 0); }
  iterator end() const { return iterator(&storage_, storage_.container_size_); }

  auto &halo() { return halo_; }
  void halo_exchange() {
    halo_.exchange_begin();
    halo_.exchange_finalize();
  }

  void barrier() const { storage_.barrier(); }
  void fence() const { storage_.fence(); }

private:
  storage<T, Allocator> storage_;
  lib::span_halo<T> halo_;
};

} // namespace mhp

template <typename T, typename Allocator>
struct fmt::formatter<mhp::storage<T, Allocator>> : formatter<string_view> {
  template <typename FmtContext>
  auto format(const mhp::storage<T, Allocator> &dv, FmtContext &ctx) {
    return format_to(ctx.out(),
                     "size: {}, comm size: {}, segment size: {}, halo bounds: "
                     "({}), data size: {}",
                     dv.container_size_, dv.comm_.size(), dv.segment_size_,
                     dv.halo_bounds_, dv.data_size_);
  }
};
