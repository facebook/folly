/*
 * Copyright 2012 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * For high-level documentation and usage examples see folly/doc/small_vector.md
 *
 * @author Jordan DeLong <delong.j@fb.com>
 */
#ifndef FOLLY_SMALL_VECTOR_H_
#define FOLLY_SMALL_VECTOR_H_

#include "Portability.h"

#include <stdexcept>
#include <cstdlib>
#include <type_traits>
#include <algorithm>
#include <iterator>
#include <cassert>

#include <boost/operators.hpp>
#include <boost/type_traits.hpp>
#include <boost/mpl/if.hpp>
#include <boost/mpl/eval_if.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/mpl/front.hpp>
#include <boost/mpl/filter_view.hpp>
#include <boost/mpl/identity.hpp>
#include <boost/mpl/placeholders.hpp>
#include <boost/mpl/empty.hpp>
#include <boost/mpl/size.hpp>
#include <boost/mpl/count.hpp>
#include <boost/mpl/max.hpp>

#include "folly/Malloc.h"

#if defined(__GNUC__) && defined(__x86_64__)
# include "folly/SmallLocks.h"
# define FB_PACKED __attribute__((packed))
#else
# define FB_PACKED
#endif

#ifdef FOLLY_HAVE_MALLOC_SIZE
  extern "C" std::size_t malloc_size(const void*);
# ifndef FOLLY_HAVE_MALLOC_USABLE_SIZE
#  define malloc_usable_size malloc_size
# endif
# ifndef malloc_usable_size
#  define malloc_usable_size malloc_size
# endif
#endif

namespace folly {

//////////////////////////////////////////////////////////////////////

namespace small_vector_policy {

//////////////////////////////////////////////////////////////////////

/*
 * A flag which makes us refuse to use the heap at all.  If we
 * overflow the in situ capacity we throw an exception.
 */
struct NoHeap;

/*
 * Passing this policy will cause small_vector to provide lock() and
 * unlock() functions using a 1-bit spin lock in the size value.
 *
 * Note that this is intended for a fairly specialized (although
 * strangely common at facebook) use case, where you have billions of
 * vectors in memory where none of them are "hot" and most of them are
 * small.  This allows you to get fine-grained locks without spending
 * a lot of memory on mutexes (the alternative of a large hashtable of
 * locks leads to extra cache misses in the lookup path).
 *
 * __x86_64__ only.
 */
struct OneBitMutex;

//////////////////////////////////////////////////////////////////////

} // small_vector_policy

//////////////////////////////////////////////////////////////////////

template<class T, std::size_t M, class A, class B, class C>
struct small_vector;

//////////////////////////////////////////////////////////////////////

namespace detail {

  /*
   * Move a range to a range of uninitialized memory.  Assumes the
   * ranges don't overlap.
   */
  template<class T>
  typename std::enable_if<
    !boost::has_trivial_copy<T>::value
  >::type
  moveToUninitialized(T* first, T* last, T* out) {
    auto const count = last - first;
    std::size_t idx = 0;
    try {
      for (; idx < count; ++first, ++idx) {
        new (&out[idx]) T(std::move(*first));
      }
    } catch (...) {
      // Even for callers trying to give the strong guarantee
      // (e.g. push_back) it's ok to assume here that we don't have to
      // move things back and that it was a copy constructor that
      // threw: if someone throws from a move constructor the effects
      // are unspecified.
      for (std::size_t i = 0; i < idx; ++i) {
        out[i].~T();
      }
      throw;
    }
  }

  // Specialization for trivially copyable types.  (TODO: change to
  // std::is_trivially_copyable when that works.)
  template<class T>
  typename std::enable_if<
    boost::has_trivial_copy<T>::value
  >::type
  moveToUninitialized(T* first, T* last, T* out) {
    std::memmove(out, first, (last - first) * sizeof *first);
  }

  /*
   * Move objects in memory to the right into some uninitialized
   * memory, where the region overlaps.  This doesn't just use
   * std::move_backward because move_backward only works if all the
   * memory is initialized to type T already.
   */
  template<class T>
  typename std::enable_if<
    !boost::has_trivial_copy<T>::value
  >::type
  moveObjectsRight(T* first, T* lastConstructed, T* realLast) {
    if (lastConstructed == realLast) {
      return;
    }

    T* end = first - 1; // Past the end going backwards.
    T* out = realLast - 1;
    T* in = lastConstructed - 1;
    try {
      for (; in != end && out >= lastConstructed; --in, --out) {
        new (out) T(std::move(*in));
      }
      for (; in != end; --in, --out) {
        *out = std::move(*in);
      }
      for (; out >= lastConstructed; --out) {
        new (out) T();
      }
    } catch (...) {
      // We want to make sure the same stuff is uninitialized memory
      // if we exit via an exception (this is to make sure we provide
      // the basic exception safety guarantee for insert functions).
      if (out < lastConstructed) {
        out = lastConstructed - 1;
      }
      for (auto it = out + 1; it != realLast; ++it) {
        it->~T();
      }
      throw;
    }
  }

  // Specialization for trivially copyable types.  The call to
  // std::move_backward here will just turn into a memmove.  (TODO:
  // change to std::is_trivially_copyable when that works.)
  template<class T>
  typename std::enable_if<
    boost::has_trivial_copy<T>::value
  >::type
  moveObjectsRight(T* first, T* lastConstructed, T* realLast) {
    std::move_backward(first, lastConstructed, realLast);
  }

  /*
   * Populate a region of memory using `op' to construct elements.  If
   * anything throws, undo what we did.
   */
  template<class T, class Function>
  void populateMemForward(T* mem, std::size_t n, Function const& op) {
    std::size_t idx = 0;
    try {
      for (int i = 0; i < n; ++i) {
        op(&mem[idx]);
        ++idx;
      }
    } catch (...) {
      for (std::size_t i = 0; i < idx; ++i) {
        mem[i].~T();
      }
      throw;
    }
  }

  template<class SizeType, bool ShouldUseHeap>
  struct IntegralSizePolicy {
    typedef SizeType InternalSizeType;

    IntegralSizePolicy() : size_(0) {}

  protected:
    std::size_t policyMaxSize() const {
      return SizeType(~kExternMask);
    }

    std::size_t doSize() const {
      return size_ & ~kExternMask;
    }

    std::size_t isExtern() const {
      return kExternMask & size_;
    }

    void setExtern(bool b) {
      if (b) {
        size_ |= kExternMask;
      } else {
        size_ &= ~kExternMask;
      }
    }

    void setSize(std::size_t sz) {
      assert(sz <= policyMaxSize());
      size_ = (kExternMask & size_) | SizeType(sz);
    }

    void swapSizePolicy(IntegralSizePolicy& o) {
      std::swap(size_, o.size_);
    }

  protected:
    static bool const kShouldUseHeap = ShouldUseHeap;

  private:
    static SizeType const kExternMask =
      kShouldUseHeap ? SizeType(1) << (sizeof(SizeType) * 8 - 1)
                     : 0;

    SizeType size_;
  };

#ifdef __x86_64__
  template<class SizeType, bool ShouldUseHeap>
  struct OneBitMutexImpl {
    typedef SizeType InternalSizeType;

    OneBitMutexImpl() { psl_.init(); }

    void lock()     const { psl_.lock(); }
    void unlock()   const { psl_.unlock(); }
    bool try_lock() const { return psl_.try_lock(); }

  protected:
    static bool const kShouldUseHeap = ShouldUseHeap;

    std::size_t policyMaxSize() const {
      return SizeType(~(SizeType(1) << kLockBit | kExternMask));
    }

    std::size_t doSize() const {
      return psl_.getData() & ~kExternMask;
    }

    std::size_t isExtern() const {
      return psl_.getData() & kExternMask;
    }

    void setExtern(bool b) {
      if (b) {
        setSize(SizeType(doSize()) | kExternMask);
      } else {
        setSize(SizeType(doSize()) & ~kExternMask);
      }
    }

    void setSize(std::size_t sz) {
      assert(sz < (std::size_t(1) << kLockBit));
      psl_.setData((kExternMask & psl_.getData()) | SizeType(sz));
    }

    void swapSizePolicy(OneBitMutexImpl& o) {
      std::swap(psl_, o.psl_);
    }

  private:
    static SizeType const kLockBit = sizeof(SizeType) * 8 - 1;
    static SizeType const kExternMask =
      kShouldUseHeap ? SizeType(1) << (sizeof(SizeType) * 8 - 2)
                     : 0;

    PicoSpinLock<SizeType,kLockBit> psl_;
  };
#else
  template<class SizeType, bool ShouldUseHeap>
  struct OneBitMutexImpl {
    static_assert(std::is_same<SizeType,void>::value,
                  "OneBitMutex only works on x86-64");
  };
#endif

  /*
   * If you're just trying to use this class, ignore everything about
   * this next small_vector_base class thing.
   *
   * The purpose of this junk is to minimize sizeof(small_vector<>)
   * and allow specifying the template parameters in whatever order is
   * convenient for the user.  There's a few extra steps here to try
   * to keep the error messages at least semi-reasonable.
   *
   * Apologies for all the black magic.
   */
  namespace mpl = boost::mpl;
  template<class Value,
           std::size_t RequestedMaxInline,
           class InPolicyA,
           class InPolicyB,
           class InPolicyC>
  struct small_vector_base {
    typedef mpl::vector<InPolicyA,InPolicyB,InPolicyC> PolicyList;

    /*
     * Determine the size type
     */
    typedef typename mpl::filter_view<
      PolicyList,
      boost::is_integral<mpl::placeholders::_1>
    >::type Integrals;
    typedef typename mpl::eval_if<
      mpl::empty<Integrals>,
      mpl::identity<std::size_t>,
      mpl::front<Integrals>
    >::type SizeType;

    static_assert(std::is_unsigned<SizeType>::value,
                  "Size type should be an unsigned integral type");
    static_assert(mpl::size<Integrals>::value == 0 ||
                    mpl::size<Integrals>::value == 1,
                  "Multiple size types specified in small_vector<>");

    /*
     * Figure out if we're supposed to supply a one-bit mutex. :)
     */
    typedef typename mpl::count<
      PolicyList,small_vector_policy::OneBitMutex
    >::type HasMutex;

    static_assert(HasMutex::value == 0 || HasMutex::value == 1,
                  "Multiple copies of small_vector_policy::OneBitMutex "
                  "supplied; this is probably a mistake");

    /*
     * Determine whether we should allow spilling to the heap or not.
     */
    typedef typename mpl::count<
      PolicyList,small_vector_policy::NoHeap
    >::type HasNoHeap;

    static_assert(HasNoHeap::value == 0 || HasNoHeap::value == 1,
                  "Multiple copies of small_vector_policy::NoHeap "
                  "supplied; this is probably a mistake");

    /*
     * Make the real policy base classes.
     */
    typedef typename mpl::if_<
      HasMutex,
      OneBitMutexImpl<SizeType,!HasNoHeap::value>,
      IntegralSizePolicy<SizeType,!HasNoHeap::value>
    >::type ActualSizePolicy;

    /*
     * Now inherit from them all.  This is done in such a convoluted
     * way to make sure we get the empty base optimizaton on all these
     * types to keep sizeof(small_vector<>) minimal.
     */
    typedef boost::totally_ordered1<
      small_vector<Value,RequestedMaxInline,InPolicyA,InPolicyB,InPolicyC>,
      ActualSizePolicy
    > type;
  };

  template <class T>
  T* pointerFlagSet(T* p) {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(p) | 1);
  }
  template <class T>
  bool pointerFlagGet(T* p) {
    return reinterpret_cast<uintptr_t>(p) & 1;
  }
  template <class T>
  T* pointerFlagClear(T* p) {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(p) & ~1);
  }
  inline void* shiftPointer(void* p, size_t sizeBytes) {
    return static_cast<char*>(p) + sizeBytes;
  }
}

//////////////////////////////////////////////////////////////////////

template<class Value,
         std::size_t RequestedMaxInline    = 1,
         class PolicyA                     = void,
         class PolicyB                     = void,
         class PolicyC                     = void>
class small_vector
  : public detail::small_vector_base<
      Value,RequestedMaxInline,PolicyA,PolicyB,PolicyC
    >::type
{
  typedef typename detail::small_vector_base<
    Value,RequestedMaxInline,PolicyA,PolicyB,PolicyC
  >::type BaseType;
  typedef typename BaseType::InternalSizeType InternalSizeType;

  /*
   * Figure out the max number of elements we should inline.  (If
   * the user asks for less inlined elements than we can fit unioned
   * into our value_type*, we will inline more than they asked.)
   */
  enum {
    MaxInline = boost::mpl::max<
                  boost::mpl::int_<sizeof(Value*) / sizeof(Value)>,
                  boost::mpl::int_<RequestedMaxInline>
                >::type::value
  };

public:
  typedef std::size_t size_type;
  typedef Value              value_type;
  typedef value_type&        reference;
  typedef value_type const&  const_reference;
  typedef value_type*        iterator;
  typedef value_type const*  const_iterator;
  typedef std::ptrdiff_t     difference_type;

  typedef std::reverse_iterator<iterator>       reverse_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

  explicit small_vector() {}

  small_vector(small_vector const& o) {
    assign(o.begin(), o.end());
  }

  small_vector(small_vector&& o) {
    *this = std::move(o);
  }

  small_vector(std::initializer_list<value_type> il) {
    constructImpl(il.begin(), il.end(), std::false_type());
  }

  explicit small_vector(size_type n, value_type const& t = value_type()) {
    doConstruct(n, t);
  }

  template<class Arg>
  explicit small_vector(Arg arg1, Arg arg2)  {
    // Forward using std::is_arithmetic to get to the proper
    // implementation; this disambiguates between the iterators and
    // (size_t, value_type) meaning for this constructor.
    constructImpl(arg1, arg2, std::is_arithmetic<Arg>());
  }

  ~small_vector() {
    for (auto& t : *this) {
      (&t)->~value_type();
    }
    if (this->isExtern()) {
      u.freeHeap();
    }
  }

  small_vector& operator=(small_vector const& o) {
    assign(o.begin(), o.end());
    return *this;
  }

  small_vector& operator=(small_vector&& o) {
    clear();
    if (!o.isExtern()) {
      makeSize(o.size());
      for (std::size_t i = 0; i < o.size(); ++i) {
        new (data() + i) value_type(std::move(o[i]));
      }
      this->setSize(o.size());
    } else {
      swap(o);
    }
    return *this;
  }

  bool operator==(small_vector const& o) const {
    return size() == o.size() && std::equal(begin(), end(), o.begin());
  }

  bool operator<(small_vector const& o) const {
    return std::lexicographical_compare(begin(), end(), o.begin(), o.end());
  }

  size_type max_size() const {
    return !BaseType::kShouldUseHeap ? MaxInline
                                     : this->policyMaxSize();
  }

  size_type size()         const { return this->doSize(); }
  bool      empty()        const { return !size(); }

  iterator       begin()         { return data(); }
  iterator       end()           { return data() + size(); }
  const_iterator begin()   const { return data(); }
  const_iterator end()     const { return data() + size(); }
  const_iterator cbegin()  const { return begin(); }
  const_iterator cend()    const { return end(); }

  reverse_iterator       rbegin()        { return reverse_iterator(end()); }
  reverse_iterator       rend()          { return reverse_iterator(begin()); }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }

  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend()   const { return rend(); }

  /*
   * Usually one of the simplest functions in a Container-like class
   * but a bit more complex here.  We have to handle all combinations
   * of in-place vs. heap between this and o.
   *
   * Basic guarantee only.  Provides the nothrow guarantee iff our
   * value_type has a nothrow move or copy constructor.
   */
  void swap(small_vector& o) {
    using std::swap; // Allow ADL on swap for our value_type.

    if (this->isExtern() && o.isExtern()) {
      this->swapSizePolicy(o);

      auto thisCapacity = this->capacity();
      auto oCapacity = o.capacity();

      std::swap(unpackHack(&u.pdata_.heap_), unpackHack(&o.u.pdata_.heap_));

      this->setCapacity(oCapacity);
      o.setCapacity(thisCapacity);

      return;
    }

    if (!this->isExtern() && !o.isExtern()) {
      auto& oldSmall = size() < o.size() ? *this : o;
      auto& oldLarge = size() < o.size() ? o : *this;

      for (size_type i = 0; i < oldSmall.size(); ++i) {
        swap(oldSmall[i], oldLarge[i]);
      }

      size_type i = oldSmall.size();
      try {
        for (; i < oldLarge.size(); ++i) {
          new (&oldSmall[i]) value_type(std::move(oldLarge[i]));
          oldLarge[i].~value_type();
        }
      } catch (...) {
        for (; i < oldLarge.size(); ++i) {
          oldLarge[i].~value_type();
        }
        oldLarge.setSize(oldSmall.size());
        throw;
      }
      this->swapSizePolicy(o);
      return;
    }

    // isExtern != o.isExtern()
    auto& oldExtern = o.isExtern() ? o : *this;
    auto& oldIntern = o.isExtern() ? *this : o;

    auto oldExternCapacity = oldExtern.capacity();
    auto oldExternHeap     = oldExtern.u.pdata_.heap_;

    auto buff = oldExtern.u.buffer();
    size_type i = 0;
    try {
      for (; i < oldIntern.size(); ++i) {
        new (&buff[i]) value_type(std::move(oldIntern[i]));
        oldIntern[i].~value_type();
      }
    } catch (...) {
      for (size_type kill = 0; kill < i; ++kill) {
        buff[kill].~value_type();
      }
      for (; i < oldIntern.size(); ++i) {
        oldIntern[i].~value_type();
      }
      oldIntern.setSize(0);
      oldExtern.u.pdata_.heap_ = oldExternHeap;
      oldExtern.setCapacity(oldExternCapacity);
      throw;
    }
    oldIntern.u.pdata_.heap_ = oldExternHeap;
    this->swapSizePolicy(o);
    oldIntern.setCapacity(oldExternCapacity);
  }

  void resize(size_type sz) {
    if (sz < size()) {
      erase(begin() + sz, end());
      return;
    }
    makeSize(sz);
    detail::populateMemForward(begin() + size(), sz - size(),
      [&] (void* p) { new (p) value_type(); }
    );
    this->setSize(sz);
  }

  void resize(size_type sz, value_type const& v) {
    if (sz < size()) {
      erase(begin() + sz, end());
      return;
    }
    makeSize(sz);
    detail::populateMemForward(begin() + size(), sz - size(),
      [&] (void* p) { new (p) value_type(v); }
    );
    this->setSize(sz);
  }

  value_type* data() noexcept {
    return this->isExtern() ? u.heap() : u.buffer();
  }

  value_type const* data() const noexcept {
    return this->isExtern() ? u.heap() : u.buffer();
  }

  template<class ...Args>
  iterator emplace(const_iterator p, Args&&... args) {
    if (p == cend()) {
      emplace_back(std::forward<Args>(args)...);
      return end() - 1;
    }

    /*
     * We implement emplace at places other than at the back with a
     * temporary for exception safety reasons.  It is possible to
     * avoid having to do this, but it becomes hard to maintain the
     * basic exception safety guarantee (unless you respond to a copy
     * constructor throwing by clearing the whole vector).
     *
     * The reason for this is that otherwise you have to destruct an
     * element before constructing this one in its place---if the
     * constructor throws, you either need a nothrow default
     * constructor or a nothrow copy/move to get something back in the
     * "gap", and the vector requirements don't guarantee we have any
     * of these.  Clearing the whole vector is a legal response in
     * this situation, but it seems like this implementation is easy
     * enough and probably better.
     */
    return insert(p, value_type(std::forward<Args>(args)...));
  }

  void reserve(size_type sz) {
    makeSize(sz);
  }

  size_type capacity() const {
    if (this->isExtern()) {
      if (u.hasCapacity()) {
        return *u.getCapacity();
      }
      return malloc_usable_size(u.pdata_.heap_) / sizeof(value_type);
    }
    return MaxInline;
  }

  void shrink_to_fit() {
    if (!this->isExtern()) {
      return;
    }

    small_vector tmp(begin(), end());
    tmp.swap(*this);
  }

  template<class ...Args>
  void emplace_back(Args&&... args) {
    // call helper function for static dispatch of special cases
    emplaceBack(std::forward<Args>(args)...);
  }

  void push_back(value_type&& t) {
    if (capacity() == size()) {
      makeSize(std::max(size_type(2), 3 * size() / 2), &t, size());
    } else {
      new (end()) value_type(std::move(t));
    }
    this->setSize(size() + 1);
  }

  void push_back(value_type const& t) {
    // Make a copy and forward to the rvalue value_type&& overload
    // above.
    push_back(value_type(t));
  }

  void pop_back() {
    erase(end() - 1);
  }

  iterator insert(const_iterator constp, value_type&& t) {
    iterator p = unconst(constp);

    if (p == end()) {
      push_back(std::move(t));
      return end() - 1;
    }

    auto offset = p - begin();

    if (capacity() == size()) {
      makeSize(size() + 1, &t, offset);
      this->setSize(this->size() + 1);
    } else {
      makeSize(size() + 1);
      detail::moveObjectsRight(data() + offset,
                               data() + size(),
                               data() + size() + 1);
      this->setSize(size() + 1);
      data()[offset] = std::move(t);
    }
    return begin() + offset;

  }

  iterator insert(const_iterator p, value_type const& t) {
    // Make a copy and forward to the rvalue value_type&& overload
    // above.
    return insert(p, value_type(t));
  }

  iterator insert(const_iterator pos, size_type n, value_type const& val) {
    auto offset = pos - begin();
    makeSize(size() + n);
    detail::moveObjectsRight(data() + offset,
                             data() + size(),
                             data() + size() + n);
    this->setSize(size() + n);
    std::generate_n(begin() + offset, n, [&] { return val; });
    return begin() + offset;
  }

  template<class Arg>
  iterator insert(const_iterator p, Arg arg1, Arg arg2) {
    // Forward using std::is_arithmetic to get to the proper
    // implementation; this disambiguates between the iterators and
    // (size_t, value_type) meaning for this function.
    return insertImpl(unconst(p), arg1, arg2, std::is_arithmetic<Arg>());
  }

  iterator insert(const_iterator p, std::initializer_list<value_type> il) {
    return insert(p, il.begin(), il.end());
  }

  iterator erase(const_iterator q) {
    std::move(unconst(q) + 1, end(), unconst(q));
    (data() + size() - 1)->~value_type();
    this->setSize(size() - 1);
    return unconst(q);
  }

  iterator erase(const_iterator q1, const_iterator q2) {
    std::move(unconst(q2), end(), unconst(q1));
    for (auto it = q1; it != end(); ++it) {
      it->~value_type();
    }
    this->setSize(size() - (q2 - q1));
    return unconst(q1);
  }

  void clear() {
    erase(begin(), end());
  }

  template<class Arg>
  void assign(Arg first, Arg last) {
    clear();
    insert(end(), first, last);
  }

  void assign(std::initializer_list<value_type> il) {
    assign(il.begin(), il.end());
  }

  void assign(size_type n, const value_type& t) {
    clear();
    insert(end(), n, t);
  }

  reference front()             { assert(!empty()); return *begin(); }
  reference back()              { assert(!empty()); return *(end() - 1); }
  const_reference front() const { assert(!empty()); return *begin(); }
  const_reference back() const  { assert(!empty()); return *(end() - 1); }

  reference operator[](size_type i) {
    assert(i < size());
    return *(begin() + i);
  }

  const_reference operator[](size_type i) const {
    assert(i < size());
    return *(begin() + i);
  }

  reference at(size_type i) {
    if (i >= size()) {
      throw std::out_of_range();
    }
    return (*this)[i];
  }

  const_reference at(size_type i) const {
    if (i >= size()) {
      throw std::out_of_range();
    }
    return (*this)[i];
  }

private:

  /*
   * This is doing the same like emplace_back, but we need this helper
   * to catch the special case - see the next overload function..
   */
  template<class ...Args>
  void emplaceBack(Args&&... args) {
    makeSize(size() + 1);
    new (end()) value_type(std::forward<Args>(args)...);
    this->setSize(size() + 1);
  }

  /*
   * Special case of emplaceBack for rvalue
   */
  void emplaceBack(value_type&& t) {
    push_back(std::move(t));
  }

  static iterator unconst(const_iterator it) {
    return const_cast<iterator>(it);
  }

  /*
   * g++ doesn't allow you to bind a non-const reference to a member
   * of a packed structure, presumably because it would make it too
   * easy to accidentally make an unaligned memory access?
   */
  template<class T> static T& unpackHack(T* p) {
    return *p;
  }

  // The std::false_type argument is part of disambiguating the
  // iterator insert functions from integral types (see insert().)
  template<class It>
  iterator insertImpl(iterator pos, It first, It last, std::false_type) {
    typedef typename std::iterator_traits<It>::iterator_category categ;
    if (std::is_same<categ,std::input_iterator_tag>::value) {
      auto offset = pos - begin();
      while (first != last) {
        pos = insert(pos, *first++);
        ++pos;
      }
      return begin() + offset;
    }

    auto distance = std::distance(first, last);
    auto offset = pos - begin();
    makeSize(size() + distance);
    detail::moveObjectsRight(data() + offset,
                             data() + size(),
                             data() + size() + distance);
    this->setSize(size() + distance);
    std::copy_n(first, distance, begin() + offset);
    return begin() + offset;
  }

  iterator insertImpl(iterator pos, size_type n, const value_type& val,
      std::true_type) {
    // The true_type means this should call the size_t,value_type
    // overload.  (See insert().)
    return insert(pos, n, val);
  }

  // The std::false_type argument came from std::is_arithmetic as part
  // of disambiguating an overload (see the comment in the
  // constructor).
  template<class It>
  void constructImpl(It first, It last, std::false_type) {
    typedef typename std::iterator_traits<It>::iterator_category categ;
    if (std::is_same<categ,std::input_iterator_tag>::value) {
      // With iterators that only allow a single pass, we can't really
      // do anything sane here.
      while (first != last) {
        push_back(*first++);
      }
      return;
    }

    auto distance = std::distance(first, last);
    makeSize(distance);
    this->setSize(distance);

    detail::populateMemForward(data(), distance,
      [&] (void* p) { new (p) value_type(*first++); }
    );
  }

  void doConstruct(size_type n, value_type const& val) {
    makeSize(n);
    this->setSize(n);
    detail::populateMemForward(data(), n,
      [&] (void* p) { new (p) value_type(val); }
    );
  }

  // The true_type means we should forward to the size_t,value_type
  // overload.
  void constructImpl(size_type n, value_type const& val, std::true_type) {
    doConstruct(n, val);
  }

  void makeSize(size_type size, value_type* v = NULL) {
    makeSize(size, v, size - 1);
  }

  /*
   * Ensure we have a large enough memory region to be size `size'.
   * Will move/copy elements if we are spilling to heap_ or needed to
   * allocate a new region, but if resized in place doesn't initialize
   * anything in the new region.  In any case doesn't change size().
   * Supports insertion of new element during reallocation by given
   * pointer to new element and position of new element.
   * NOTE: If reallocation is not needed, and new element should be
   * inserted in the middle of vector (not at the end), do the move
   * objects and insertion outside the function, otherwise exception is thrown.
   */
  void makeSize(size_type size, value_type* v, size_type pos) {
    if (size > this->max_size()) {
      throw std::length_error("max_size exceeded in small_vector");
    }
    if (size <= this->capacity()) {
      return;
    }

    auto needBytes = size * sizeof(value_type);
    // If the capacity isn't explicitly stored inline, but the heap
    // allocation is grown to over some threshold, we should store
    // a capacity at the front of the heap allocation.
    bool heapifyCapacity =
      !kHasInlineCapacity && needBytes > kHeapifyCapacityThreshold;
    if (heapifyCapacity) {
      needBytes += kHeapifyCapacitySize;
    }
    auto const sizeBytes = goodMallocSize(needBytes);
    void* newh = std::malloc(sizeBytes);
    if (!newh) {
      throw std::bad_alloc();
    }
    // We expect newh to be at least 2-aligned, because we want to
    // use its least significant bit as a flag.
    assert(!detail::pointerFlagGet(newh));

    value_type* newp = static_cast<value_type*>(
      heapifyCapacity ?
        detail::shiftPointer(newh, kHeapifyCapacitySize) :
        newh);

    if (v != NULL) {
      // move new element
      try {
        new (&newp[pos]) value_type(std::move(*v));
      } catch (...) {
        std::free(newh);
        throw;
      }

      // move old elements to the left of the new one
      try {
        detail::moveToUninitialized(begin(), begin() + pos, newp);
      } catch (...) {
        newp[pos].~value_type();
        std::free(newh);
        throw;
      }

      // move old elements to the right of the new one
      try {
        if (pos < size-1) {
          detail::moveToUninitialized(begin() + pos, end(), newp + pos + 1);
        }
      } catch (...) {
        for (size_type i = 0; i <= pos; ++i) {
          newp[i].~value_type();
        }
        std::free(newh);
        throw;
      }
    } else {
      // move without inserting new element
      try {
        detail::moveToUninitialized(begin(), end(), newp);
      } catch (...) {
        std::free(newh);
        throw;
      }
    }
    for (auto& val : *this) {
      val.~value_type();
    }

    if (this->isExtern()) {
      u.freeHeap();
    }
    auto availableSizeBytes = sizeBytes;
    if (heapifyCapacity) {
      u.pdata_.heap_ = detail::pointerFlagSet(newh);
      availableSizeBytes -= kHeapifyCapacitySize;
    } else {
      u.pdata_.heap_ = newh;
    }
    this->setExtern(true);
    this->setCapacity(availableSizeBytes / sizeof(value_type));
  }

  /*
   * This will set the capacity field, stored inline in the storage_ field
   * if there is sufficient room to store it.
   */
  void setCapacity(size_type newCapacity) {
    assert(this->isExtern());
    if (u.hasCapacity()) {
      assert(newCapacity < std::numeric_limits<InternalSizeType>::max());
      *u.getCapacity() = InternalSizeType(newCapacity);
    }
  }

private:
  struct HeapPtrWithCapacity {
    void* heap_;
    InternalSizeType capacity_;

    InternalSizeType* getCapacity() {
      return &capacity_;
    }
  } FB_PACKED;

  struct HeapPtr {
    // Lower order bit of heap_ is used as flag to indicate whether capacity is
    // stored at the front of the heap allocation.
    void* heap_;

    InternalSizeType* getCapacity() {
      assert(detail::pointerFlagGet(heap_));
      return static_cast<InternalSizeType*>(
        detail::pointerFlagClear(heap_));
    }
  } FB_PACKED;

#if defined(__x86_64_)
  typedef unsigned char InlineStorageType[sizeof(value_type) * MaxInline];
#else
  typedef typename std::aligned_storage<
    sizeof(value_type) * MaxInline,
    alignof(value_type)
  >::type InlineStorageType;
#endif

  static bool const kHasInlineCapacity =
    sizeof(HeapPtrWithCapacity) < sizeof(InlineStorageType);

  // This value should we multiple of word size.
  static size_t const kHeapifyCapacitySize = sizeof(
    typename std::aligned_storage<
      sizeof(InternalSizeType),
      alignof(value_type)
    >::type);
  // Threshold to control capacity heapifying.
  static size_t const kHeapifyCapacityThreshold =
    100 * kHeapifyCapacitySize;

  typedef typename std::conditional<
    kHasInlineCapacity,
    HeapPtrWithCapacity,
    HeapPtr
  >::type PointerType;

  union Data {
    explicit Data() { pdata_.heap_ = 0; }

    PointerType pdata_;
    InlineStorageType storage_;

    value_type* buffer() noexcept {
      void* vp = &storage_;
      return static_cast<value_type*>(vp);
    }
    value_type const* buffer() const noexcept {
      return const_cast<Data*>(this)->buffer();
    }
    value_type* heap() noexcept {
      if (kHasInlineCapacity || !detail::pointerFlagGet(pdata_.heap_)) {
        return static_cast<value_type*>(pdata_.heap_);
      }
      return static_cast<value_type*>(
        detail::shiftPointer(
          detail::pointerFlagClear(pdata_.heap_), kHeapifyCapacitySize));
    }
    value_type const* heap() const noexcept {
      return const_cast<Data*>(this)->heap();
    }

    bool hasCapacity() const {
      return kHasInlineCapacity || detail::pointerFlagGet(pdata_.heap_);
    }
    InternalSizeType* getCapacity() {
      return pdata_.getCapacity();
    }
    InternalSizeType* getCapacity() const {
      return const_cast<Data*>(this)->getCapacity();
    }

    void freeHeap() {
      auto vp = detail::pointerFlagClear(pdata_.heap_);
      std::free(vp);
    }
  } FB_PACKED u;
} FB_PACKED;

//////////////////////////////////////////////////////////////////////

// Basic guarantee only, or provides the nothrow guarantee iff T has a
// nothrow move or copy constructor.
template<class T, std::size_t MaxInline, class A, class B, class C>
void swap(small_vector<T,MaxInline,A,B,C>& a,
          small_vector<T,MaxInline,A,B,C>& b) {
  a.swap(b);
}

//////////////////////////////////////////////////////////////////////

}

#ifdef FB_PACKED
# undef FB_PACKED
#endif

#endif
