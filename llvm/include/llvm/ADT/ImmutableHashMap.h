//===- ImmutableHashMap.h - Immutable hash map interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ImmutableHashMap class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_IMMUTABLEHASHMAP_H
#define LLVM_ADT_IMMUTABLEHASHMAP_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableHashSet.h"

namespace llvm {

template <class KeyTy, class DataTy> struct ImmutableHashMapInfo {
  using value_type = std::pair<KeyTy, DataTy>;
  using const_value_type = const value_type;
  using value_type_ref = value_type &;
  using const_value_type_ref = const value_type &;
  using key_type = const KeyTy;
  using key_type_ref = const KeyTy &;
  using data_type = const DataTy;
  using data_type_ref = const DataTy &;
  using ValueProfileInfo = ImutProfileInfo<DataTy>;
  using KeyProfileInfo = ImutProfileInfo<KeyTy>;

  static void Profile(FoldingSetNodeID &ID, const_value_type_ref Data) {
    KeyProfileInfo::Profile(ID, Data.first);
    ValueProfileInfo::Profile(ID, Data.second);
  }

  static detail::hash_t getHash(const_value_type_ref Value) {
    return getHash(getKey(Value));
  }
  static detail::hash_t getHash(key_type_ref Key) {
    FoldingSetNodeID ID;
    KeyProfileInfo::Profile(ID, Key);
    return ID.ComputeHash();
  }
  static bool areEqual(const_value_type_ref LHS, const_value_type_ref RHS) {
    return areKeysEqual(LHS, RHS) && areEqual(getData(LHS), getData(RHS));
  }
  static bool areKeysEqual(const_value_type_ref LHS, const_value_type_ref RHS) {
    return areKeysEqual(getKey(LHS), getKey(RHS));
  }
  static bool areKeysEqual(const_value_type_ref LHS, key_type_ref RHS) {
    return areKeysEqual(getKey(LHS), RHS);
  }
  static bool areKeysEqual(key_type_ref LHS, const_value_type_ref RHS) {
    return areKeysEqual(RHS, LHS);
  }

  static bool areEqual(data_type_ref LHS, data_type_ref RHS) {
    return LHS == RHS;
  }
  static bool areKeysEqual(key_type_ref LHS, key_type_ref RHS) {
    return LHS == RHS;
  }
  static key_type_ref getKey(const_value_type_ref Value) { return Value.first; }
  static data_type_ref getData(const_value_type_ref Value) {
    return Value.second;
  }
};

template <class KeyTy, class DataTy,
          class ValueInfo = ImmutableHashMapInfo<KeyTy, DataTy>>
class ImmutableHashMap {
public:
  using value_type = typename ValueInfo::value_type;
  using const_value_type = typename ValueInfo::const_value_type;
  using value_type_ref = typename ValueInfo::value_type_ref;
  using const_value_type_ref = typename ValueInfo::const_value_type_ref;
  using key_type = typename ValueInfo::key_type;
  using key_type_ref = typename ValueInfo::key_type_ref;
  using data_type = typename ValueInfo::data_type;
  using data_type_ref = typename ValueInfo::data_type_ref;

  using Trie = detail::HAMT<ValueInfo>;
  /* implicit */ ImmutableHashMap(Trie From) : Impl(From) {}

  class Factory {
  public:
    Factory() = default;
    Factory(BumpPtrAllocator &Alloc) : Impl(Alloc) {}

    LLVM_NODISCARD ImmutableHashMap getEmptyMap() { return Impl.getEmptySet(); }
    LLVM_NODISCARD ImmutableHashMap add(ImmutableHashMap Original,
                                        key_type_ref K, data_type_ref D) {
      return Impl.add(Original.Impl, value_type(K, D));
    }
    LLVM_NODISCARD ImmutableHashMap remove(ImmutableHashMap Original,
                                           key_type_ref Key) {
      return Impl.remove(Original.Impl, Key);
    }

  private:
    typename Trie::Factory Impl;
  };

  class iterator : public Trie::Iterator {
  public:
    using Trie::Iterator::Iterator;

    key_type_ref getKey() const { return (*this)->first; }
    data_type_ref getData() const { return (*this)->second; }
  };

  iterator begin() const { return iterator(Impl); }
  iterator end() const { return iterator(Impl, typename iterator::EndTag{}); }

  bool contains(key_type_ref Key) const { return Impl.find(Key); }
  data_type *lookup(key_type_ref Key) const {
    if (const_value_type *Value = Impl.find(Key))
      return &ValueInfo::getData(*Value);
    return nullptr;
  }
  size_t getSize() const { return Impl.getSize(); }
  bool isEmpty() const { return Impl.isEmpty(); }

  using RawRootType = typename Trie::RawRootType;
  RawRootType getRoot() const { return Impl.getRoot(); }

  ImmutableHashMap(RawRootType Root) : Impl(Root) {}

  bool operator==(const ImmutableHashMap &RHS) const {
    return Impl.isEqual(RHS.Impl);
  }

  bool operator!=(const ImmutableHashMap &RHS) const {
    return !operator==(RHS);
  }

  static void Profile(FoldingSetNodeID &ID, const ImmutableHashMap &S) {
    ID.AddPointer(S.getRoot());
  }

  void Profile(FoldingSetNodeID &ID) const { return Profile(ID, *this); }

private:
  Trie Impl;
};

} // end namespace llvm

#endif // LLVM_ADT_IMMUTABLEHASHMAP_H
