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
function threadSleep = [](chrono::milliseconds duration){
  return [=](){
    this_thread::sleep_for(duration);
  };
};

TEST(PoolSize, Default) {
  // Create a thread pool without any threads.
  Pool a{};
  EXPECT_EQ(a.getThreadCount(), 0);
  a.start();
  EXPECT_EQ(a.getThreadCount(), thread::hardware_concurrency());
  a.join();
  EXPECT_EQ(a.getThreadCount(), 0);
}

TEST(PoolSize, Specified) {
  // Create a thread pool with 2 threads.
  Pool a{2};
  EXPECT_EQ(a.getThreadCount(), 0);
  a.start();
  EXPECT_EQ(a.getThreadCount(), 2);
  a.join();
  EXPECT_EQ(a.getThreadCount(), 0);
}

TEST(PoolSize, Change) {
  // Create a thread pool, increase the size after start.
  Pool a{2};
  EXPECT_EQ(a.getThreadCount(), 0);
  a.start();
  EXPECT_EQ(a.getThreadCount(), 2);
  a.setThreadCount(3);
  EXPECT_EQ(a.getThreadCount(), 3);
}

TEST(JobQueue, Count) {
  // Create a thread pool with no threads, enqueue jobs that will
  // never be processed.
  Pool a{0};
  EXPECT_EQ(a.getJobQueueCount(), 0);
  a.enqueue({emptyFunc});
  EXPECT_EQ(a.getJobQueueCount(), 1);
  a.enqueue({emptyFunc});
  this_thread::sleep_for(1ms);
  EXPECT_EQ(a.getJobQueueCount(), 2);
}

TEST(StopJoin, Compare) {
  // Compare .stop() vs .join().
  Pool a{3};
  a.enqueue({threadSleep(10ms)});
  a.enqueue({threadSleep(10ms)});
  a.start();
  this_thread::sleep_for(1ms);
  EXPECT_EQ(a.getThreadCount(), 3);
  EXPECT_EQ(a.getTerminatedThreadCount(), 0);
  a.stop();
  this_thread::sleep_for(1ms);
  EXPECT_EQ(a.getTerminatedThreadCount(), 1);
  EXPECT_EQ(a.getThreadCount(), 3);
  a.join();
  EXPECT_EQ(a.getTerminatedThreadCount(), 0);
  EXPECT_EQ(a.getThreadCount(), 0);
}

TEST(Counts, All) {
  // Test thread counts.
  Pool a{3};
  EXPECT_EQ(a.getWaitingThreadCount(), 0);
  EXPECT_EQ(a.getRunningThreadCount(), 0);
  a.start();
  this_thread::sleep_for(1ms);
  EXPECT_EQ(a.getWaitingThreadCount(), 3);
  EXPECT_EQ(a.getRunningThreadCount(), 0);
  a.enqueue({threadSleep(10ms)});
  a.enqueue({threadSleep(10ms)});
  this_thread::sleep_for(1ms);
  EXPECT_EQ(a.getWaitingThreadCount(), 1);
  EXPECT_EQ(a.getRunningThreadCount(), 2);
}

int main(int argc, char** argv) {
  startGlobalPool();
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  endGlobalPool();
  return result;
}

