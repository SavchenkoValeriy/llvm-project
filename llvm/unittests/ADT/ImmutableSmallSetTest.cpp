//===---- ImmutableSmallSetTest.cpp - ImmutableSmallSet unit tests --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ImmutableSmallSet.h"
#include "llvm/ADT/Sequence.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {
class ImmutableSmallSetTest : public testing::Test {
public:
  using Set = ImmutableSmallSet<int>;
  Set::Factory F;
};

TEST_F(ImmutableSmallSetTest, EmptyIntSetTest) {
  EXPECT_TRUE(F.getEmptySet() == F.getEmptySet());
  EXPECT_FALSE(F.getEmptySet() != F.getEmptySet());
  EXPECT_TRUE(F.getEmptySet().isEmpty());

  Set S = F.getEmptySet();
  EXPECT_TRUE(S.begin() == S.end());
  EXPECT_FALSE(S.begin() != S.end());
}

TEST_F(ImmutableSmallSetTest, OneElemIntSetTest) {
  Set S = F.getEmptySet();

  Set S2 = F.add(S, 3);
  EXPECT_TRUE(S.isEmpty());
  EXPECT_FALSE(S2.isEmpty());
  EXPECT_FALSE(S == S2);
  EXPECT_TRUE(S != S2);
  EXPECT_FALSE(S.contains(3));
  EXPECT_TRUE(S2.contains(3));
  EXPECT_FALSE(S2.begin() == S2.end());
  EXPECT_TRUE(S2.begin() != S2.end());

  Set S3 = F.add(S, 2);
  EXPECT_TRUE(S.isEmpty());
  EXPECT_FALSE(S3.isEmpty());
  EXPECT_FALSE(S == S3);
  EXPECT_TRUE(S != S3);
  EXPECT_FALSE(S.contains(2));
  EXPECT_TRUE(S3.contains(2));

  EXPECT_FALSE(S2 == S3);
  EXPECT_TRUE(S2 != S3);
  EXPECT_FALSE(S2.contains(2));
  EXPECT_FALSE(S3.contains(3));
}

TEST_F(ImmutableSmallSetTest, MultiElemIntSetTest) {
  Set S = F.getEmptySet();

  Set S2 = F.add(F.add(F.add(S, 3), 4), 5);
  Set S3 = F.add(F.add(F.add(S2, 9), 20), 43);
  Set S4 = F.add(S2, 9);

  EXPECT_TRUE(S.isEmpty());
  EXPECT_FALSE(S2.isEmpty());
  EXPECT_FALSE(S3.isEmpty());
  EXPECT_FALSE(S4.isEmpty());

  EXPECT_FALSE(S.contains(3));
  EXPECT_FALSE(S.contains(9));

  EXPECT_TRUE(S2.contains(3));
  EXPECT_TRUE(S2.contains(4));
  EXPECT_TRUE(S2.contains(5));
  EXPECT_FALSE(S2.contains(9));
  EXPECT_FALSE(S2.contains(0));

  EXPECT_TRUE(S3.contains(43));
  EXPECT_TRUE(S3.contains(20));
  EXPECT_TRUE(S3.contains(9));
  EXPECT_TRUE(S3.contains(3));
  EXPECT_TRUE(S3.contains(4));
  EXPECT_TRUE(S3.contains(5));
  EXPECT_FALSE(S3.contains(0));

  EXPECT_TRUE(S4.contains(9));
  EXPECT_TRUE(S4.contains(3));
  EXPECT_TRUE(S4.contains(4));
  EXPECT_TRUE(S4.contains(5));
  EXPECT_FALSE(S4.contains(20));
  EXPECT_FALSE(S4.contains(43));
}

TEST_F(ImmutableSmallSetTest, RemoveIntSetTest) {
  Set S = F.getEmptySet();

  Set S2 = F.add(F.add(S, 4), 5);
  Set S3 = F.add(S2, 3);
  Set S4 = F.remove(S3, 3);

  EXPECT_TRUE(S3.contains(3));
  EXPECT_FALSE(S2.contains(3));
  EXPECT_FALSE(S4.contains(3));

  EXPECT_TRUE(S2 == S4);
  EXPECT_TRUE(S3 != S2);
  EXPECT_TRUE(S3 != S4);

  EXPECT_TRUE(S3.contains(4));
  EXPECT_TRUE(S3.contains(5));

  EXPECT_TRUE(S4.contains(4));
  EXPECT_TRUE(S4.contains(5));
}

TEST_F(ImmutableSmallSetTest, LargeBatchTest) {
  Set S1 = F.getEmptySet();
  for (int Element : llvm::seq<int>(0, 100)) {
    S1 = F.add(S1, Element);
    for (int SmallerElement : llvm::seq_inclusive<int>(0, Element))
      EXPECT_TRUE(S1.contains(SmallerElement));
  }

  Set S2 = F.getEmptySet();
  for (int Element : S1) {
    S2 = F.add(S2, Element);
  }

  EXPECT_TRUE(S1 == S2);
}

TEST_F(ImmutableSmallSetTest, AddAndRemoveTest) {
  Set S = F.getEmptySet();
  for (int Element : llvm::seq<int>(0, 10)) {
    S = F.add(S, Element);
  }

  for (int Element : llvm::seq<int>(0, 10)) {
    S = F.remove(S, Element);
  }

  EXPECT_TRUE(S.isEmpty());
}

TEST_F(ImmutableSmallSetTest, AddAndRemoveLargeTest) {
  Set S = F.getEmptySet();
  for (int Element : llvm::seq<int>(0, 1000)) {
    S = F.add(S, Element);
  }

  for (int Element : llvm::seq<int>(0, 1000)) {
    S = F.remove(S, Element);
  }

  EXPECT_TRUE(S.isEmpty());
}

TEST_F(ImmutableSmallSetTest, Silly) {
  Set S = F.getEmptySet();
  for (int Element : llvm::seq<int>(0, 10)) {
    S = F.add(S, Element);
  }

  for (Set::iterator It = S.begin(), E = S.end(); It != E;) {
    auto C = It;
    if (C++ == E)
      break;
    EXPECT_TRUE(S.contains(*(It++)));
  }
}
} // end anonymous namespace
