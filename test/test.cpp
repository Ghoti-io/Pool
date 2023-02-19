/**
 * @file
 *
 * Test the general thread pool behavior.
 */

#include <gtest/gtest.h>
#include <iostream>
#include "pool.hpp"

using namespace std;
using namespace Ghoti::Pool;

TEST(Declare, Null) {
  Pool a{};
  a.stop();
  EXPECT_EQ(0, 0);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
