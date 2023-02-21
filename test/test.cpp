/**
 * @file
 *
 * Test the general thread pool behavior.
 */

#include <gtest/gtest.h>
#include <functional>
#include <iostream>
#include <thread>
#include "pool.hpp"

using namespace std;
using namespace Ghoti::Pool;

function emptyFunc = [](){};
function threadSleep = [](size_t seconds){
  this_thread::sleep_for(chrono::seconds{seconds});
};

TEST(PoolSize, Default) {
  Pool a{};
  EXPECT_EQ(a.getThreadCount(), 0);
  a.start();
  EXPECT_EQ(a.getThreadCount(), thread::hardware_concurrency());
  a.stop();
  EXPECT_EQ(a.getThreadCount(), 0);
}

TEST(PoolSize, Specified) {
  Pool a{2};
  EXPECT_EQ(a.getThreadCount(), 0);
  a.start();
  EXPECT_EQ(a.getThreadCount(), 2);
  a.stop();
  EXPECT_EQ(a.getThreadCount(), 0);
}

TEST(JobQueue, Count) {
  Pool a{0};
  EXPECT_EQ(a.getJobQueueCount(), 0);
  a.enqueue({emptyFunc});
  EXPECT_EQ(a.getJobQueueCount(), 1);
  a.enqueue({emptyFunc});
  EXPECT_EQ(a.getJobQueueCount(), 2);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

