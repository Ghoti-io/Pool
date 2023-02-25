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

TEST(JoinGlobalPool, Repeated) {
  // Verify that calling joinGlobalPool() repeatedly doesn't break anything.
  EXPECT_NO_THROW(joinGlobalPool());
  EXPECT_NO_THROW(joinGlobalPool());
  EXPECT_NO_THROW(joinGlobalPool());
}

TEST(JoinGlobalPool, AsJoin) {
  // Verify that calling joinGlobalPool() while a thread is running
  // will indeed work.

  // First, verify that no other threads are running.
  EXPECT_NO_THROW(joinGlobalPool());

  {
    // Create a pool of two threads and one job
    Pool a{2};
    a.enqueue({threadSleep(10ms)});
    a.start();

    // Wait to ensure that the thread pool has time to start the thread.
    this_thread::sleep_for(1ms);
  }

  // Verify that there is one thread running, even though the Pool has been
  // destroyed.
  EXPECT_EQ(getGlobalPoolThreadCount(), 2);

  // Verify that the global join will indeed wait until all threads are joined.
  joinGlobalPool();
  EXPECT_EQ(getGlobalPoolThreadCount(), 0);
}

TEST(Pool, IndependentThreads) {
  // Verify that two pools of threads create independent threads in the pool.
  EXPECT_NO_THROW(joinGlobalPool());

  Pool a{2};
  Pool b{3};

  // Verify that there are no threads starting out.
  EXPECT_EQ(getGlobalPoolThreadCount(), 0);

  // Verify that 2 threads are created for `a`.
  a.start();
  EXPECT_EQ(getGlobalPoolThreadCount(), 2);

  // Verify that 3 additional threads are created for `b`.
  b.start();
  EXPECT_EQ(getGlobalPoolThreadCount(), 5);

  // Verify that 3 threads were joined for `b` and that two remain for `a`.
  b.join();
  EXPECT_EQ(getGlobalPoolThreadCount(), 2);
}

TEST(PoolSize, Default) {
  // Create a thread pool without specifying the number of threads.
  Pool a{};

  // Verify that no threads are created yet.
  EXPECT_EQ(a.getThreadCount(), 0);
  EXPECT_EQ(a.getTerminatedThreadCount(), 0);
  EXPECT_EQ(a.getWaitingThreadCount(), 0);
  EXPECT_EQ(a.getRunningThreadCount(), 0);

  // Verify that the correct default number of threads are created.
  a.start();
  EXPECT_EQ(a.getThreadCount(), thread::hardware_concurrency());
  EXPECT_EQ(a.getTerminatedThreadCount(), 0);
  EXPECT_EQ(a.getWaitingThreadCount(), 0);
  EXPECT_EQ(a.getRunningThreadCount(), 0);

  // Verify that all threads have been stopped and cleaned up.
  a.join();
  EXPECT_EQ(a.getThreadCount(), 0);
  EXPECT_EQ(a.getTerminatedThreadCount(), thread::hardware_concurrency());
  EXPECT_EQ(a.getWaitingThreadCount(), 0);
  EXPECT_EQ(a.getRunningThreadCount(), 0);
}

TEST(PoolSize, Specified) {
  // Create a thread pool with 2 threads.
  // Sleep as needed to allow the threads to react to the request.
  Pool a{2};

  // Verify that no threads are created yet.
  EXPECT_EQ(a.getThreadCount(), 0);
  EXPECT_EQ(a.getTerminatedThreadCount(), 0);
  EXPECT_EQ(a.getWaitingThreadCount(), 0);
  EXPECT_EQ(a.getRunningThreadCount(), 0);

  // Verify that 2 threads have now been created.
  a.start();
  this_thread::sleep_for(1ms);
  EXPECT_EQ(a.getThreadCount(), 2);
  EXPECT_EQ(a.getTerminatedThreadCount(), 0);
  EXPECT_EQ(a.getWaitingThreadCount(), 2);
  EXPECT_EQ(a.getRunningThreadCount(), 0);

  // Verify that increasing the thread count works.
  a.setThreadCount(3);
  this_thread::sleep_for(1ms);
  EXPECT_EQ(a.getThreadCount(), 3);
  EXPECT_EQ(a.getTerminatedThreadCount(), 0);
  EXPECT_EQ(a.getWaitingThreadCount(), 3);
  EXPECT_EQ(a.getRunningThreadCount(), 0);

  // Verify that decreasing the thread count works.
  a.setThreadCount(1);
  this_thread::sleep_for(1ms);
  EXPECT_EQ(a.getThreadCount(), 1);
  EXPECT_EQ(a.getTerminatedThreadCount(), 2);
  EXPECT_EQ(a.getWaitingThreadCount(), 1);
  EXPECT_EQ(a.getRunningThreadCount(), 0);

  // Verify that all threads have been stopped and cleaned up.
  a.join();
  EXPECT_EQ(a.getThreadCount(), 0);
  EXPECT_EQ(a.getTerminatedThreadCount(), 3);
  EXPECT_EQ(a.getWaitingThreadCount(), 0);
  EXPECT_EQ(a.getRunningThreadCount(), 0);
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
  EXPECT_EQ(a.getThreadCount(), 2);
  EXPECT_EQ(a.getWaitingThreadCount(), 0);
  EXPECT_EQ(a.getRunningThreadCount(), 2);

  // Join all threads in the pool.
  a.join();

  // Verify that the counts of the threads and their disposition are correct.
  EXPECT_EQ(a.getTerminatedThreadCount(), 3);
  EXPECT_EQ(a.getThreadCount(), 0);
  EXPECT_EQ(a.getWaitingThreadCount(), 0);
  EXPECT_EQ(a.getRunningThreadCount(), 0);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

