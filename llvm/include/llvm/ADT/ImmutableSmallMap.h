//===- ImmutableSmallMap.h - sadas ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  asdas
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_IMMUTABLESMALLMAP_H
#define LLVM_ADT_IMMUTABLESMALLMAP_H

#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/ImmutableSmallSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
template <class KeyT, class ValT, unsigned S = 16,
          class ValInfo = ImutKeyValueInfo<KeyT, ValT>>
class ImmutableSmallMap {
public:
  using value_type = typename ValInfo::value_type;
  using value_type_ref = typename ValInfo::value_type_ref;
  using key_type = typename ValInfo::key_type;
  using key_type_ref = typename ValInfo::key_type_ref;
  using data_type = typename ValInfo::data_type;
  using data_type_ref = typename ValInfo::data_type_ref;

  using SmallMap = detail::SmallSetVector<ValT, ValInfo, S>;
  using SmallPtrTy = SmallMap *;
  using LargeMap = ImmutableMap<ValT, KeyT, ValInfo>;
  using LargePtrTy = typename LargeMap::TreeTy *;
  using RawPtrTy = void *;

  explicit ImmutableSmallMap(RawPtrTy Raw) {
    RawPtr = RawPtr.getFromOpaqueValue(Raw);
    retain();
  }

  ImmutableSmallMap(const ImmutableSmallMap &Other)
      : ImmutableSmallMap(Other.getRawPtrWithoutRetain()) {}
  ImmutableSmallMap &operator=(const ImmutableSmallMap &Other) {
    if (this != &Other) {
      release();
      RawPtr = Other.RawPtr;
      retain();
    }
    return *this;
  }

  ImmutableSmallMap(ImmutableSmallMap &&Other) : RawPtr(Other.RawPtr) {
    Other.RawPtr = nullptr;
  }
  ImmutableSmallMap &operator=(ImmutableSmallMap &&Other) {
    release();
    RawPtr = Other.RawPtr;
    Other.RawPtr = nullptr;
    return *this;
  }

  ~ImmutableSmallMap() { release(); }

  bool contains(key_type_ref K) const {
    if (LLVM_LIKELY(isSmall())) {
      SmallPtrTy ThisAsSmall = getSmall();
      return ThisAsSmall && ThisAsSmall->contains(K);
    }

    return getLarge().contains(K);
  }
  data_type *lookup(key_type_ref K) const {
    if (LLVM_LIKELY(isSmall())) {
      SmallPtrTy ThisAsSmall = getSmall();
      if (!ThisAsSmall)
        return nullptr;
      const value_type *It = ThisAsSmall->find(K);
      return It ? &It->second : nullptr;
    }

    return getLarge().lookup(K);
  }
  LLVM_NODISCARD bool isEmpty() const { return RawPtr.isNull(); }

  bool operator==(const ImmutableSmallMap &RHS) const {
    return RawPtr && RHS.RawPtr
               ? areEqual(*this, RHS)
               : getRawPtrWithoutRetain() == RHS.getRawPtrWithoutRetain();
  }
  bool operator!=(const ImmutableSmallMap &RHS) const {
    return !(*this == RHS);
  }
  bool operator<(const ImmutableSmallMap &RHS) const {
    if (isSmall() && RHS.isSmall())
      return getSmall()->size() < RHS.getSmall()->size();
    if (isSmall())
      return true;
    if (RHS.isSmall())
      return false;
    return getLarge().getHeight() < RHS.getLarge().getHeight();
  }

  RawPtrTy getRawPtr() {
    retain();
    return getRawPtrWithoutRetain();
  }

  RawPtrTy getRawPtrWithoutRetain() const { return RawPtr.getOpaqueValue(); }

  class iterator
      : public iterator_facade_base<iterator, std::bidirectional_iterator_tag,
                                    const value_type> {
  public:
    using Base = iterator_facade_base<iterator, std::bidirectional_iterator_tag,
                                      const value_type>;

    iterator() = default;
    explicit iterator(typename SmallMap::iterator It)
        : IsSmall(true), SmallIterator(It) {}
    explicit iterator(typename LargeMap::iterator It)
        : IsSmall(false), LargeIterator(It) {}

    iterator(const iterator &Other)
        : IsSmall(Other.IsSmall), SmallIterator(Other.SmallIterator),
          LargeIterator(Other.LargeIterator) {}
    iterator &operator=(const iterator &Other) {
      if (this != &Other) {
        IsSmall = Other.IsSmall;
        SmallIterator = Other.SmallIterator;
        LargeIterator = Other.LargeIterator;
      }
      return *this;
    }

    bool operator==(const iterator &Other) const {
      return IsSmall ? SmallIterator == Other.SmallIterator
                     : LargeIterator == Other.LargeIterator;
    }

    const value_type &operator*() const {
      return IsSmall ? *SmallIterator : *LargeIterator;
    }
    iterator &operator++() {
      if (IsSmall)
        ++SmallIterator;
      else
        ++LargeIterator;
      return *this;
    }
    iterator operator++(int Dummy) { return Base::operator++(Dummy); }
    iterator &operator--() {
      if (IsSmall)
        --SmallIterator;
      else
        --LargeIterator;
      return *this;
    }
    iterator operator--(int Dummy) { return Base::operator--(Dummy); }

    key_type_ref getKey() const {
      return IsSmall ? SmallIterator->first : LargeIterator.getKey();
    }
    data_type_ref getData() const {
      return IsSmall ? SmallIterator->second : LargeIterator.getData();
    }

  private:
    bool IsSmall = true;
    typename SmallMap::iterator SmallIterator;
    typename LargeMap::iterator LargeIterator;
  };

  iterator begin() const {
    if (RawPtr.isNull())
      return iterator{};

    if (LLVM_LIKELY(isSmall()))
      return iterator{getSmall()->begin()};

    return iterator{getLarge().begin()};
  }

  iterator end() const {
    if (RawPtr.isNull())
      return {};

    if (LLVM_LIKELY(isSmall()))
      return iterator{getSmall()->end()};

    return iterator{getLarge().end()};
  }

  static void Profile(FoldingSetNodeID &ID, const ImmutableSmallMap &Map) {
    ID.AddPointer(Map.getRawPtrWithoutRetain());
  }

  void Profile(FoldingSetNodeID &ID) const { return Profile(ID, *this); }

  class Factory {
  public:
    Factory(BumpPtrAllocator &Alloc) : SmallF(Alloc), LargeF(Alloc) {}
    Factory() = default;

    Factory(const Factory &) = delete;
    void operator=(const Factory &) = delete;
    Factory(Factory &&) = delete;
    Factory &operator=(Factory &&) = delete;

    LLVM_NODISCARD ImmutableSmallMap getEmptyMap() {
      return {static_cast<SmallPtrTy>(nullptr)};
    }
    LLVM_NODISCARD ImmutableSmallMap add(ImmutableSmallMap Origin,
                                         key_type_ref K, data_type_ref D) {
      if (LLVM_LIKELY(Origin.isSmall())) {
        SmallPtrTy OriginAsSmall = Origin.getSmall();
        if (LLVM_LIKELY(!OriginAsSmall || OriginAsSmall->size() < S)) {
          OriginAsSmall =
              SmallF.add(OriginAsSmall, std::pair<key_type, data_type>(K, D));
          OriginAsSmall->retain();
          return OriginAsSmall;
        }

        LargeMap EnlargedOrigin = LargeF.getEmptyMap();
        llvm::for_each(OriginAsSmall->getElements(),
                       [this, &EnlargedOrigin](value_type_ref StoredInSmall) {
                         EnlargedOrigin =
                             LargeF.add(EnlargedOrigin, StoredInSmall.first,
                                        StoredInSmall.second);
                       });
        EnlargedOrigin = LargeF.add(EnlargedOrigin, K, D);
        return EnlargedOrigin.getRoot();
      }

      return LargeF.add(Origin.getLarge(), K, D).getRoot();
    }
    LLVM_NODISCARD ImmutableSmallMap remove(ImmutableSmallMap Origin,
                                            key_type_ref K) {
      if (LLVM_LIKELY(Origin.isSmall())) {
        SmallPtrTy Result = SmallF.remove(Origin.getSmall(), K);
        if (Result)
          Result->retain();
        return Result;
      }

      return LargeF.remove(Origin.getLarge(), K).getRoot();
    }

    BumpPtrAllocator &getAllocator() { return SmallF.getAllocator(); }

  private:
    typename SmallMap::Factory SmallF;
    typename LargeMap::Factory LargeF;
  };

private:
  ImmutableSmallMap(SmallPtrTy Impl) : RawPtr(Impl) {}
  ImmutableSmallMap(LargePtrTy Impl) : RawPtr(Impl) {}

  void retain() {
    if (RawPtr.isNull())
      return;

    if (LLVM_LIKELY(isSmall()))
      getSmall()->retain();
    else
      getLargePtr()->retain();
  }

  void release() {
    if (RawPtr.isNull())
      return;

    if (LLVM_LIKELY(isSmall()))
      getSmall()->release();
    else
      getLargePtr()->release();
  }

  SmallPtrTy getSmall() const { return RawPtr.template get<SmallPtrTy>(); }

  LargePtrTy getLargePtr() const { return RawPtr.template get<LargePtrTy>(); }

  LargeMap getLarge() const { return LargeMap(getLargePtr()); }

  static bool areEqual(const ImmutableSmallMap &LHS,
                       const ImmutableSmallMap &RHS) {
    switch (LHS.isSmall() + (RHS.isSmall() << 1)) {
    case 3: // both are small
      return LHS.getSmall()->isEqual(*RHS.getSmall());
    case 0: // both are large
      return LHS.getLarge() == RHS.getLarge();
    case 1:
      return LHS.getSmall()->isEqual(*RHS.getLargePtr());
    case 2:
      return RHS.getSmall()->isEqual(*LHS.getLargePtr());
    default:
      llvm_unreachable("Impossible result");
    }
  }

  LLVM_NODISCARD bool isSmall() const {
    return RawPtr.template is<SmallPtrTy>();
  }

  PointerUnion<SmallPtrTy, LargePtrTy> RawPtr;
};
} // end namespace llvm

#endif // LLVM_ADT_IMMUTABLESMALLMAP_H
