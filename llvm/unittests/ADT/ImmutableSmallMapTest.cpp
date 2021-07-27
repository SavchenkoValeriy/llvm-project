//===---- ImmutableSmallMapTest.cpp - ImmutableSmallMap unit tests --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ImmutableSmallMap.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

class ImmutableSmallMapTest : public testing::Test {
public:
  using Map = ImmutableSmallMap<int, int>;
  Map::Factory F;
};

TEST_F(ImmutableSmallMapTest, EmptyIntMapTest) {
  EXPECT_TRUE(F.getEmptyMap() == F.getEmptyMap());
  EXPECT_FALSE(F.getEmptyMap() != F.getEmptyMap());
  EXPECT_TRUE(F.getEmptyMap().isEmpty());

  Map S = F.getEmptyMap();
  EXPECT_TRUE(S.begin() == S.end());
  EXPECT_FALSE(S.begin() != S.end());
}

TEST_F(ImmutableSmallMapTest, MultiElemIntMapTest) {
  Map S = F.getEmptyMap();

  Map S2 = F.add(F.add(F.add(S, 3, 10), 4, 11), 5, 12);

  EXPECT_TRUE(S.isEmpty());
  EXPECT_FALSE(S2.isEmpty());

  EXPECT_EQ(nullptr, S.lookup(3));
  EXPECT_EQ(nullptr, S.lookup(9));

  EXPECT_EQ(10, *S2.lookup(3));
  EXPECT_EQ(11, *S2.lookup(4));
  EXPECT_EQ(12, *S2.lookup(5));
}

} // namespace
