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

#include "llvm/ADT/ImmutableHashSet.h"

namespace llvm {

template <class KeyTy, class DataTy> struct ImmutableHasMapInfo {
  using value_type = std::pair<KeyTy, DataTy>;
  using const_value_type = const value_type;
  using value_type_ref = value_type &;
  using const_value_type_ref = const value_type &;
  using key_type = const KeyTy;
  using key_type_ref = const KeyTy &;
  using data_type = const DataTy;
  using data_type_ref = const DataTy &;
  using ProfileInfo = ImutProfileInfo<KeyTy>;

  static detail::hash_t getHash(const_value_type_ref Value) {
    return getHash(getKey(Value));
  }
  static detail::hash_t getHash(key_type_ref Key) {
    FoldingSetNodeID ID;
    ProfileInfo::Profile(ID, Key);
    return ID.ComputeHash();
  }
  static bool areEqual(const_value_type_ref LHS, const_value_type_ref RHS) {
    return areEqual(LHS.first, RHS.first);
  }
  static bool areEqual(const_value_type_ref LHS, key_type_ref RHS) {
    return areEqual(LHS.first, RHS);
  }
  static bool areEqual(key_type_ref LHS, const_value_type_ref RHS) {
    return areEqual(RHS, LHS);
  }

  static bool areEqual(key_type_ref LHS, key_type_ref RHS) {
    return LHS == RHS;
  }
  static key_type_ref getKey(const_value_type_ref Value) { return Value.first; }

  static data_type_ref getData(const_value_type_ref Value) {
    return Value.second;
  }
};

template <class KeyTy, class DataTy,
          class ValueInfo = ImmutableHasMapInfo<KeyTy, DataTy>>
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

  using iterator = typename Trie::Iterator;
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

  using RootType = typename Trie::RootType;
  RootType getRoot() const { return Impl.getRoot(); }

  ImmutableHashMap(RootType Root) : Impl(Root) {}

  bool operator==(const ImmutableHashMap &RHS) const {
    return Impl.isEqual(RHS.Impl);
  }

  bool operator!=(const ImmutableHashMap &RHS) const {
    return !operator==(RHS);
  }

private:
  Trie Impl;
};

} // end namespace llvm

#endif // LLVM_ADT_IMMUTABLEHASHMAP_H
