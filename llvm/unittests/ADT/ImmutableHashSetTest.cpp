//===------- ImmutableHashSetTest.cpp - ImmutableHashSet unit tests -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ImmutableHashSet.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

template <class T> struct AlwaysCollisionTrait : public ImmutableHasSetInfo<T> {
  static detail::hash_t
  getHash(typename ImmutableHasSetInfo<T>::key_type_ref Key) {
    return 1u;
  }
};

template <class T>
struct FrequentCollisionTrait : public ImmutableHasSetInfo<T> {
  static detail::hash_t
  getHash(typename ImmutableHasSetInfo<T>::key_type_ref Key) {
    return ImmutableHasSetInfo<T>::getHash(Key) % 32;
  }
};

TEST(ImmutableHashSetTest, SimpleAddContainsTest) {
  using Set = ImmutableHashSet<int>;
  Set::Factory F;

  Set Empty = F.getEmptySet();

  Set OneTwoThree = Empty;
  OneTwoThree = F.add(OneTwoThree, 1);
  EXPECT_EQ(OneTwoThree.getSize(), 1u);
  OneTwoThree = F.add(OneTwoThree, 2);
  EXPECT_EQ(OneTwoThree.getSize(), 2u);
  OneTwoThree = F.add(OneTwoThree, 3);
  EXPECT_EQ(OneTwoThree.getSize(), 3u);

  EXPECT_TRUE(OneTwoThree.contains(1));
  EXPECT_TRUE(OneTwoThree.contains(2));
  EXPECT_TRUE(OneTwoThree.contains(3));
}

TEST(ImmutableHashSetTest, ConsecutiveAddContainsTest) {
  using Set = ImmutableHashSet<int>;
  constexpr int MAX = 10000;
  Set::Factory F;

  Set Test = F.getEmptySet();

  for (int Element = 0; Element < MAX; ++Element) {
    Test = F.add(Test, Element);
  }

  for (int Element = 0; Element < MAX; ++Element) {
    EXPECT_TRUE(Test.contains(Element)) << " doesn't contain " << Element;
  }
}

TEST(ImmutableHashSetTest, ConsecutiveCollisionAddContainsTest) {
  using Set = ImmutableHashSet<int, AlwaysCollisionTrait<int>>;
  constexpr int MAX = 100;
  Set::Factory F;

  Set Test = F.getEmptySet();

  for (int Element = 0; Element < MAX; ++Element) {
    Test = F.add(Test, Element);
  }

  for (int Element = 0; Element < MAX; ++Element) {
    EXPECT_TRUE(Test.contains(Element)) << " doesn't contain " << Element;
  }
}

TEST(ImmutableHashSetTest, ConsecutiveFrequentCollisionAddContainsTest) {
  using Set = ImmutableHashSet<int, FrequentCollisionTrait<int>>;
  constexpr int MAX = 10000;
  Set::Factory F;

  Set Test = F.getEmptySet();

  for (int Element = 0; Element < MAX; ++Element) {
    Test = F.add(Test, Element);
  }

  for (int Element = 0; Element < MAX; ++Element) {
    EXPECT_TRUE(Test.contains(Element)) << " doesn't contain " << Element;
  }
}

TEST(ImmutableHashSetTest, SimpleAddEqualsTest) {
  using Set = ImmutableHashSet<int>;
  Set::Factory F;

  Set First = F.getEmptySet();
  Set Second = F.getEmptySet();
  EXPECT_EQ(First, F.getEmptySet());
  First = F.add(First, 1);
  EXPECT_NE(First, F.getEmptySet());
  Second = F.add(First, 3);
  EXPECT_NE(First, Second);
  First = F.add(First, 2);
  First = F.add(First, 5);
  EXPECT_NE(First, Second);
  EXPECT_EQ(First, First);
  Set Third = F.add(Second, 3);
  EXPECT_EQ(Second, Third);
  First = F.add(First, 3);
  Third = F.add(Third, 5);
  Third = F.add(Third, 2);
  EXPECT_NE(Second, Third);
  EXPECT_EQ(First, Third);
}

TEST(ImmutableHashSetTest, CollisionEqualsTest) {
  using Set = ImmutableHashSet<int, AlwaysCollisionTrait<int>>;
  Set::Factory F;
  constexpr int MAX = 10;

  Set First = F.getEmptySet();
  for (int Element = 0; Element <= MAX; ++Element) {
    First = F.add(First, Element);
  }

  Set Second = F.getEmptySet();
  for (int Element = MAX; Element >= 0; --Element) {
    Second = F.add(Second, Element);
  }

  EXPECT_EQ(First, Second);
}

} // namespace
