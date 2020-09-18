//===------- ImmutableHashSetTest.cpp - ImmutableHashSet unit tests -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ImmutableHashSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

template <class Set> std::string toString(const Set &S) {
  std::string SetRep;
  llvm::raw_string_ostream SS(SetRep);
  SS << "{ ";
  interleave(S, SS, ", ");
  SS << " }";
  return SS.str();
}

// We need it here for better fail diagnostics from gtest.
template <class T, class Info>
LLVM_ATTRIBUTE_UNUSED static std::ostream &
operator<<(std::ostream &OS, const ImmutableHashSet<T, Info> &Set) {
  return OS << toString(Set);
}

template <class T> struct DefaultInfo : public ImmutableHasSetInfo<T> {};

template <class T> struct AlwaysCollisionInfo : public ImmutableHasSetInfo<T> {
  static detail::hash_t
  getHash(typename ImmutableHasSetInfo<T>::key_type_ref Key) {
    return 1u;
  }
};

template <class T>
struct FrequentCollisionInfo : public ImmutableHasSetInfo<T> {
  static detail::hash_t
  getHash(typename ImmutableHasSetInfo<T>::key_type_ref Key) {
    return ImmutableHasSetInfo<T>::getHash(Key) % 32;
  }
};

template <class Info> class ImmutableHashSetTest : public testing::Test {
public:
  using Set = ImmutableHashSet<int, Info>;
  typename Set::Factory F;
};

using Infos = ::testing::Types<DefaultInfo<int>, AlwaysCollisionInfo<int>,
                               FrequentCollisionInfo<int>>;
TYPED_TEST_CASE(ImmutableHashSetTest, Infos);

TYPED_TEST(ImmutableHashSetTest, SimpleAddContainsTest) {
  using Set = typename TestFixture::Set;
  Set Empty = this->F.getEmptySet();

  Set OneTwoThree = Empty;
  OneTwoThree = this->F.add(OneTwoThree, 1);
  EXPECT_EQ(OneTwoThree.getSize(), 1u);
  OneTwoThree = this->F.add(OneTwoThree, 2);
  EXPECT_EQ(OneTwoThree.getSize(), 2u);
  OneTwoThree = this->F.add(OneTwoThree, 3);
  EXPECT_EQ(OneTwoThree.getSize(), 3u);

  EXPECT_TRUE(OneTwoThree.contains(1));
  EXPECT_TRUE(OneTwoThree.contains(2));
  EXPECT_TRUE(OneTwoThree.contains(3));
}

TYPED_TEST(ImmutableHashSetTest, ConsecutiveAddContainsTest) {
  using Set = typename TestFixture::Set;
  constexpr int MAX = 10000;

  Set Test = this->F.getEmptySet();

  for (int Element = 0; Element < MAX; ++Element) {
    Test = this->F.add(Test, Element);
  }

  for (int Element = 0; Element < MAX; ++Element) {
    EXPECT_TRUE(Test.contains(Element)) << " doesn't contain " << Element;
  }
}

TYPED_TEST(ImmutableHashSetTest, SimpleAddEqualsTest) {
  using Set = typename TestFixture::Set;

  Set First = this->F.getEmptySet();
  Set Second = this->F.getEmptySet();
  EXPECT_EQ(First, this->F.getEmptySet());
  First = this->F.add(First, 1);
  EXPECT_NE(First, this->F.getEmptySet());
  Second = this->F.add(First, 3);
  EXPECT_NE(First, Second);
  First = this->F.add(First, 2);
  First = this->F.add(First, 5);
  EXPECT_NE(First, Second);
  EXPECT_EQ(First, First);
  Set Third = this->F.add(Second, 3);
  EXPECT_EQ(Second, Third);
  First = this->F.add(First, 3);
  Third = this->F.add(Third, 5);
  Third = this->F.add(Third, 2);
  EXPECT_NE(Second, Third);
  EXPECT_EQ(First, Third);
}

TYPED_TEST(ImmutableHashSetTest, EqualsTest) {
  using Set = typename TestFixture::Set;
  constexpr int MAX = 1000;

  Set First = this->F.getEmptySet();
  for (int Element = 0; Element <= MAX; ++Element) {
    First = this->F.add(First, Element);
  }

  Set Second = this->F.getEmptySet();
  for (int Element = MAX; Element >= 0; --Element) {
    Second = this->F.add(Second, Element);
  }

  EXPECT_EQ(First, Second);
}

TYPED_TEST(ImmutableHashSetTest, EmptyIteratorTest) {
  using Set = typename TestFixture::Set;

  Set Empty = this->F.getEmptySet();
  for (const int &Value : Empty) {
    FAIL() << " unexpected value: " << Value;
  }
}

TYPED_TEST(ImmutableHashSetTest, IteratorSizeTest) {
  using Set = typename TestFixture::Set;
  constexpr int MAX = 1000;

  Set Test = this->F.getEmptySet();
  for (int Element = 0; Element <= MAX; ++Element) {
    Test = this->F.add(Test, Element);
  }

  std::size_t Size = 0;
  for (LLVM_ATTRIBUTE_UNUSED int Element : Test) {
    ++Size;
  }

  EXPECT_EQ(Test.getSize(), Size);
}

TYPED_TEST(ImmutableHashSetTest, ReIteratorAddTest) {
  using Set = typename TestFixture::Set;
  constexpr int MAX = 1000;

  Set First = this->F.getEmptySet();
  for (int Element = 0; Element <= MAX; ++Element) {
    First = this->F.add(First, Element);
  }

  Set Second = this->F.getEmptySet();
  for (int Element : First) {
    Second = this->F.add(Second, Element);
  }

  EXPECT_EQ(First, Second);
}

TYPED_TEST(ImmutableHashSetTest, SimpleRemoveTest) {
  using Set = typename TestFixture::Set;

  Set Test = this->F.getEmptySet();
  Test = this->F.add(Test, 1);
  Test = this->F.add(Test, 2);
  Test = this->F.add(Test, 3);

  Test = this->F.remove(Test, 3);
  EXPECT_EQ(Test.getSize(), 2u);
  Test = this->F.remove(Test, 1);
  EXPECT_EQ(Test.getSize(), 1u);
  // Nothing bad should happen if we remove element that is not in the set
  Test = this->F.remove(Test, 1);
  EXPECT_EQ(Test.getSize(), 1u);

  Test = this->F.remove(Test, 2);
  EXPECT_TRUE(Test.isEmpty());
  EXPECT_EQ(Test, this->F.getEmptySet());

  // Removing elements from an empty set should also work
  Test = this->F.remove(Test, 4);
  EXPECT_TRUE(Test.isEmpty());
  EXPECT_EQ(Test, this->F.getEmptySet());
}

TYPED_TEST(ImmutableHashSetTest, ConsecutiveAddRemoveTest) {
  using Set = typename TestFixture::Set;
  constexpr int MAX = 10000;

  Set Test = this->F.getEmptySet();

  for (int Element = 0; Element <= MAX; ++Element) {
    Test = this->F.add(Test, Element);
  }

  EXPECT_EQ(Test.getSize(), (size_t)(MAX + 1));

  for (int Element = MAX; Element >= 0; --Element) {
    EXPECT_TRUE(Test.contains(Element)) << " doesn't contain " << Element;
    Test = this->F.remove(Test, Element);
    EXPECT_FALSE(Test.contains(Element)) << " contains " << Element;
  }

  EXPECT_EQ(Test, this->F.getEmptySet());
}

TYPED_TEST(ImmutableHashSetTest, ReplenishTest) {
  using Set = typename TestFixture::Set;
  constexpr int MAX = 10000, MID = MAX / 2;

  Set First = this->F.getEmptySet();

  for (int Element = 0; Element <= MAX; ++Element) {
    First = this->F.add(First, Element);
  }

  EXPECT_EQ(First.getSize(), (size_t)(MAX + 1));

  Set Second = First;
  for (int Element = MAX; Element >= MID; --Element) {
    Second = this->F.remove(Second, Element);
  }

  for (int Element = MID; Element <= MAX; ++Element) {
    Second = this->F.add(Second, Element);
  }

  EXPECT_EQ(First, Second);
}

TYPED_TEST(ImmutableHashSetTest, IterationCompletenessTest) {
  using Set = typename TestFixture::Set;
  constexpr int MAX = 10000;

  Set First = this->F.getEmptySet();

  for (int Element = 0; Element <= MAX; ++Element) {
    First = this->F.add(First, Element);
  }

  Set Second = First;

  EXPECT_EQ(First.getSize(), (size_t)(MAX + 1));

  for (int Element : Second) {
    EXPECT_TRUE(First.contains(Element)) << " doesn't contain " << Element;
    First = this->F.remove(First, Element);
  }

  // We should've iterated through all of the values
  EXPECT_EQ(First, this->F.getEmptySet());
}

} // namespace
