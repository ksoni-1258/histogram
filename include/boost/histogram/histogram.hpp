// Copyright 2015-2018 Hans Dembinski
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_HISTOGRAM_HISTOGRAM_HPP
#define BOOST_HISTOGRAM_HISTOGRAM_HPP

#include <boost/histogram/detail/axes.hpp>
#include <boost/histogram/detail/common_type.hpp>
#include <boost/histogram/detail/linearize.hpp>
#include <boost/histogram/detail/meta.hpp>
#include <boost/histogram/fwd.hpp>
#include <boost/mp11/list.hpp>
#include <boost/throw_exception.hpp>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost {
namespace histogram {

/** Central class of the histogram library.

  Histogram uses the call operator to insert data, it follows accumulator semantics like
  [Boost.Accumulators](https://www.boost.org/doc/libs/develop/doc/html/accumulators.html).
  All other methods query information about the histogram.

  Use factory functions like
  [make_histogram](histogram/reference.html#header.boost.histogram.make_histogram_hpp) and
  [make_profile](histogram/reference.html#header.boost.histogram.make_profile_hpp) to
  conveniently create histograms.

  Use the [indexed](boost/histogram/indexed.html) range generator to iterate over filled
  histograms, which is convenient and faster than hand-written loops for multi-dimensional
  histograms.
 */
template <class Axes, class Storage>
class histogram {
public:
  static_assert(mp11::mp_size<Axes>::value > 0, "at least one axis required");

public:
  using axes_type = Axes;
  using storage_type = Storage;
  using value_type = typename storage_type::value_type;
  // typedefs for boost::range_iterator
  using iterator = typename storage_type::iterator;
  using const_iterator = typename storage_type::const_iterator;

  histogram() = default;
  histogram(const histogram& rhs) = default;
  histogram(histogram&& rhs) = default;
  histogram& operator=(const histogram& rhs) = default;
  histogram& operator=(histogram&& rhs) = default;

  template <class A, class S>
  explicit histogram(histogram<A, S>&& rhs) : storage_(std::move(rhs.storage_)) {
    detail::axes_assign(axes_, rhs.axes_);
  }

  template <class A, class S>
  explicit histogram(const histogram<A, S>& rhs) : storage_(rhs.storage_) {
    detail::axes_assign(axes_, rhs.axes_);
  }

  template <class A, class S>
  histogram& operator=(histogram<A, S>&& rhs) {
    detail::axes_assign(axes_, std::move(rhs.axes_));
    storage_ = std::move(rhs.storage_);
    return *this;
  }

  template <class A, class S>
  histogram& operator=(const histogram<A, S>& rhs) {
    detail::axes_assign(axes_, rhs.axes_);
    storage_ = rhs.storage_;
    return *this;
  }

  template <class A, class S>
  explicit histogram(A&& a, S&& s)
      : axes_(std::forward<A>(a)), storage_(std::forward<S>(s)) {
    storage_.reset(detail::bincount(axes_));
  }

  template <class A, class = detail::requires_axes<A>>
  explicit histogram(A&& a) : histogram(std::forward<A>(a), storage_type()) {}

  /// Number of axes (dimensions).
  unsigned rank() const noexcept { return detail::get_size(axes_); }

  /// Total number of bins (including underflow/overflow).
  std::size_t size() const noexcept { return storage_.size(); }

  /// Reset all bins to default initialized values.
  void reset() { storage_.reset(storage_.size()); }

  /// Get N-th axis using a compile-time number.
  /// This version is more efficient than the one accepting a run-time number.
  template <unsigned N = 0>
  decltype(auto) axis(std::integral_constant<unsigned, N> = {}) const {
    detail::axis_index_is_valid(axes_, N);
    return detail::axis_get<N>(axes_);
  }

  /// Get N-th axis with run-time number.
  /// Use version that accepts a compile-time number, if possible.
  decltype(auto) axis(unsigned i) const {
    detail::axis_index_is_valid(axes_, i);
    return detail::axis_get(axes_, i);
  }

  /// Apply unary functor/function to each axis.
  template <class Unary>
  auto for_each_axis(Unary&& unary) const {
    return detail::for_each_axis(axes_, std::forward<Unary>(unary));
  }

  /** Fill histogram with values and optional weight or sample.

   Arguments are passed in order to the axis objects. Passing an argument type that is
   not convertible to the value type accepted by the axis or passing the wrong number
   of arguments causes a throw of std::invalid_argument.

   # Axis with multiple arguments
   If the histogram contains an axis which accepts a std::tuple of arguments, the
   arguments for that axis need to passed as a std::tuple, for example,
   std::make_tuple(1.2, 2.3). If the histogram contains only this axis and no other,
   the arguments can be passed directly.

   # Weights
   An optional weight can be passed as the first or last argument with the weight
   helper function. Compilation fails if the storage elements do not support weights.

   # Samples
   If the storage elements accept samples, pass them with the sample helper function
   in addition to the axis arguments, which can be the first or last argument. The
   sample helper function can pass one or more arguments to the storage element. If
   samples and weights are used together, they can be passed in any order at the beginning
   or end of the argument list.
  */
  //@{
  template <class... Ts>
  auto operator()(const Ts&... ts) {
    return operator()(std::forward_as_tuple(ts...));
  }

  template <class... Ts>
  auto operator()(const std::tuple<Ts...>& t) {
    return detail::fill(storage_, axes_, t);
  }
  //@}

  /// Add values of another histogram.
  template <class A, class S>
  histogram& operator+=(const histogram<A, S>& rhs) {
    if (!detail::axes_equal(axes_, rhs.axes_))
      BOOST_THROW_EXCEPTION(std::invalid_argument("axes of histograms differ"));
    storage_ += rhs.storage_;
    return *this;
  }

  /// Multiply all values with scalar.
  histogram& operator*=(const double x) {
    storage_ *= x;
    return *this;
  }

  /// Divide all values by scalar.
  histogram& operator/=(const double x) { return operator*=(1.0 / x); }

  /// Access cell value at integral indices.
  /**
    You can pass indices as individual arguments, as a std::tuple of integers, or as an
    interable range of integers. Passing the wrong number of arguments causes a throw of
    std::invalid_argument. Passing an index which is out of bounds causes a throw of
    std::out_of_range.
  */
  template <class... Ts>
  decltype(auto) at(int t, Ts... ts) {
    return at(std::forward_as_tuple(t, ts...));
  }

  /// Access cell value at integral indices (read-only).
  /// @copydoc at(int t, Ts... ts)
  template <class... Ts>
  decltype(auto) at(int t, Ts... ts) const {
    return at(std::forward_as_tuple(t, ts...));
  }
  //@}

  /// Access cell value at integral indices stored in std::tuple.
  /// @copydoc at(int t, Ts... ts)
  template <typename... Ts>
  decltype(auto) at(const std::tuple<Ts...>& t) {
    const auto idx = detail::at(axes_, t);
    if (!idx) BOOST_THROW_EXCEPTION(std::out_of_range("indices out of bounds"));
    return storage_[*idx];
  }

  /// Access cell value at integral indices stored in std::tuple (read-only).
  /// @copydoc at(int t, Ts... ts)
  template <typename... Ts>
  decltype(auto) at(const std::tuple<Ts...>& t) const {
    const auto idx = detail::at(axes_, t);
    if (!idx) BOOST_THROW_EXCEPTION(std::out_of_range("indices out of bounds"));
    return storage_[*idx];
  }

  /// Access cell value at integral indices stored in iterable.
  /// @copydoc at(int t, Ts... ts)
  template <class Iterable, class = detail::requires_iterable<Iterable>>
  decltype(auto) at(const Iterable& c) {
    const auto idx = detail::at(axes_, c);
    if (!idx) BOOST_THROW_EXCEPTION(std::out_of_range("indices out of bounds"));
    return storage_[*idx];
  }

  /// Access cell value at integral indices stored in iterable (read-only).
  /// @copydoc at(int t, Ts... ts)
  template <class Iterable, class = detail::requires_iterable<Iterable>>
  decltype(auto) at(const Iterable& c) const {
    const auto idx = detail::at(axes_, c);
    if (!idx) BOOST_THROW_EXCEPTION(std::out_of_range("indices out of bounds"));
    return storage_[*idx];
  }

  /// Access value at index (number for rank = 1, else std::tuple or iterable).
  template <class T>
  decltype(auto) operator[](const T& t) {
    return at(t);
  }

  /// Access value at index (number for rank = 1, else std::tuple or iterable; read-only).
  template <class T>
  decltype(auto) operator[](const T& t) const {
    return at(t);
  }

  /// Equality operator, tests equality for all axes and the storage.
  template <class A, class S>
  bool operator==(const histogram<A, S>& rhs) const noexcept {
    return detail::axes_equal(axes_, rhs.axes_) && storage_ == rhs.storage_;
  }

  /// Negation of the equality operator.
  template <class A, class S>
  bool operator!=(const histogram<A, S>& rhs) const noexcept {
    return !operator==(rhs);
  }

  iterator begin() noexcept { return storage_.begin(); }
  iterator end() noexcept { return storage_.end(); }

  const_iterator begin() const noexcept { return storage_.begin(); }
  const_iterator end() const noexcept { return storage_.end(); }

private:
  axes_type axes_;
  storage_type storage_;

  template <class A, class S>
  friend class histogram;
  friend struct unsafe_access;
};

template <class A, class S>
auto operator*(const histogram<A, S>& h, double x) {
  auto r = histogram<A, detail::common_storage<S, dense_storage<double>>>(h);
  return r *= x;
}

template <class A, class S>
auto operator*(double x, const histogram<A, S>& h) {
  return h * x;
}

template <class A, class S>
auto operator/(const histogram<A, S>& h, double x) {
  return h * (1.0 / x);
}

template <class A1, class S1, class A2, class S2>
auto operator+(const histogram<A1, S1>& a, const histogram<A2, S2>& b) {
  auto r = histogram<detail::common_axes<A1, A2>, detail::common_storage<S1, S2>>(a);
  return r += b;
}

template <class A, class S>
auto operator+(histogram<A, S>&& a, const histogram<A, S>& b) {
  return a += b;
}

template <class A, class S>
auto operator+(const histogram<A, S>& a, histogram<A, S>&& b) {
  return b += a;
}

#if __cpp_deduction_guides >= 201606

template <class Axes>
histogram(Axes&& axes)->histogram<detail::naked<Axes>, default_storage>;

template <class Axes, class Storage>
histogram(Axes&& axes, Storage&& storage)
    ->histogram<detail::naked<Axes>, detail::naked<Storage>>;

#endif

/// Helper function to mark argument as weight
template <typename T>
auto weight(T&& t) noexcept {
  return weight_type<T>{std::forward<T>(t)};
}

/// Helper function to mark arguments as sample
template <typename... Ts>
auto sample(Ts&&... ts) noexcept {
  return sample_type<std::tuple<Ts&&...>>{std::forward_as_tuple(std::forward<Ts>(ts)...)};
}

} // namespace histogram
} // namespace boost

#endif
