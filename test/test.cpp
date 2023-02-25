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

// Helper function that doesn't do anything.
function emptyFunc = [](){};

// Helper function that will sleep the executing thread for a specified number
// of milliseconds.
function threadSleep = [](chrono::milliseconds duration){
  return [=](){
    this_thread::sleep_for(duration);
  };
};

TEST(PoolSize, Default) {
  // Create a thread pool without specifying the number of threads.
  Pool a{};

  // Verify that no threads are created yet.
  EXPECT_EQ(a.getThreadCount(), 0);

  // Verify that the correct default number of threads are created.
  a.start();
  EXPECT_EQ(a.getThreadCount(), thread::hardware_concurrency());

  // Verify that all threads have been stopped and cleaned up.
  a.join();
  EXPECT_EQ(a.getThreadCount(), 0);
}

TEST(PoolSize, Specified) {
  // Create a thread pool with 2 threads.
  Pool a{2};

  // Verify that no threads are created yet.
  EXPECT_EQ(a.getThreadCount(), 0);

  // Verify that 2 threads have now been created.
  a.start();
  EXPECT_EQ(a.getThreadCount(), 2);

  // Verify that increasing the thread count works.
  a.setThreadCount(3);
  EXPECT_EQ(a.getThreadCount(), 3);

  // Verify that all threads have been stopped and cleaned up.
  a.join();
  EXPECT_EQ(a.getThreadCount(), 0);
}

TEST(JobQueue, Count) {
  // Create a thread pool with no threads, enqueue jobs that will
  // never be processed.
  Pool a{0};

  // Verify that a job is properly enqueued (even when the pool has not been
  // started yet).
  a.enqueue({emptyFunc});
  EXPECT_EQ(a.getJobQueueCount(), 1);

  // Verify that multiple jobs will properly enqueue.
  a.enqueue({emptyFunc});
  EXPECT_EQ(a.getJobQueueCount(), 2);

  // Verify that sleeping does not modify this count.
  this_thread::sleep_for(1ms);
  EXPECT_EQ(a.getJobQueueCount(), 2);
}

TEST(StopJoin, Compare) {
  // Compare .stop() vs .join().
  Pool a{3};

  // Enqueue 2 jobs.  Start the pool.
  a.enqueue({threadSleep(10ms)});
  a.enqueue({threadSleep(10ms)});
  a.start();

  // Sleep to ensure that threads have time to claim the jobs.
  this_thread::sleep_for(1ms);

  // Verify that the counts of the threads and their disposition are correct.
  EXPECT_EQ(a.getThreadCount(), 3);
  EXPECT_EQ(a.getTerminatedThreadCount(), 0);
  EXPECT_EQ(a.getWaitingThreadCount(), 1);
  EXPECT_EQ(a.getRunningThreadCount(), 2);

  // Stop the pool.
  a.stop();

  // Sleep to ensure that threads have time to respond.
  this_thread::sleep_for(1ms);

  // Verify that the counts of the threads and their disposition are correct.
  EXPECT_EQ(a.getTerminatedThreadCount(), 1);
  EXPECT_EQ(a.getThreadCount(), 3);
  EXPECT_EQ(a.getWaitingThreadCount(), 0);
  EXPECT_EQ(a.getRunningThreadCount(), 2);

  // Join all threads in the pool.
  a.join();

  // Verify that the counts of the threads and their disposition are correct.
  EXPECT_EQ(a.getTerminatedThreadCount(), 0);
  EXPECT_EQ(a.getThreadCount(), 0);
  EXPECT_EQ(a.getWaitingThreadCount(), 0);
  EXPECT_EQ(a.getRunningThreadCount(), 0);
}

int main(int argc, char** argv) {
  startGlobalPool();
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  endGlobalPool();
  return result;
}

