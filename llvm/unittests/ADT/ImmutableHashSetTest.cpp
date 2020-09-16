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
  constexpr int MAX = 1000000;
  Set::Factory F;

  Set Test = F.getEmptySet();

  for (int i = 0; i < MAX; ++i) {
    Test = F.add(Test, i);
  }

  for (int i = 0; i < MAX; ++i) {
    EXPECT_TRUE(Test.contains(i)) << " doesn't contain " << i;
  }
}

} // namespace
