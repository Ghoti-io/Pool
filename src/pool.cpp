/**
 * @file
 *
 * Code for the Pool thread pool.
 */

#include "pool.hpp"
#include <iostream>

using namespace std;
using namespace Ghoti::Pool;

Pool::Pool() : terminate{true} {
  // Get the number of logical cores.
  this->thread_target_count = thread::hardware_concurrency();
}

Pool::Pool(size_t thread_count) : terminate{true} {
  // Get the number of logical cores.
  this->thread_target_count = thread_count;
}

bool Pool::enqueue(Job && job) {
  {
    unique_lock<mutex> lock{this->queueMutex};
    this->jobs.emplace(move(job));
  }

  // Notify a thread that a job is available.
  if (!this->terminate) {
    this->mutexCondition.notify_one();
  }
  return true;
}

void Pool::stop() {
  // Don't try to stop an already-stopped pool.
  if (this->terminate) {
    return;
  }

  // Set the stop condition.
  {
    unique_lock<mutex> lock{this->queueMutex};
    this->terminate = true;
  }

  // Wake up all threads so that they will terminate themselves.
  this->mutexCondition.notify_all();

  // Join the threads.
  for (auto & thread : this->threads) {
    thread.join();
  }

  // Clean up the threads.
  this->threads.clear();
}

void Pool::start() {
  // Don't try to start the pool if it is already running.
  if (!this->terminate) {
    return;
  }

  // Set the stop condition.
  {
    unique_lock<mutex> lock{this->queueMutex};
    this->terminate = false;
  }

  // Create threads in the pool.
  this->threads.reserve(this->thread_target_count);

  for (size_t i = 0; i < this->thread_target_count; ++i) {
    jthread thread{&Pool::Pool::threadLoop, this};
    this->threadsWaiting[thread.get_id()] = false;
    this->threads.emplace_back(move(thread));
  }
}

size_t Pool::getJobQueueCount() {
  unique_lock<mutex> lock{this->queueMutex};
  return this->jobs.size();
}

size_t Pool::getThreadCount() const {
  return this->threads.size();
}

size_t Pool::getWaitingThreadCount() const {
  size_t count{0};
  return count;
}

size_t Pool::getTerminatedThreadCount() const {
  size_t count{0};
  return count;
}

size_t Pool::getRunningThreadCount() const {
  size_t count{0};
  return count;
}

void Pool::threadLoop() {
  auto thread_id = this_thread::get_id();

  // The thread loop will continue forever unless the terminate flag is set.
  while (true) {
    Job job;
    {
      this->threadsWaiting[thread_id] = true;

      unique_lock<mutex> lock{this->queueMutex};

      // Wake up if there is a job or if the terminate flag is set.
      this->mutexCondition.wait(lock, [this] {
        return !this->jobs.empty() || this->terminate;
      });

      this->threadsWaiting[thread_id] = false;

      // Terminate the thread.
      if (this->terminate) {
        return;
      }

      // Claim a job.
      job = move(this->jobs.front());
      this->jobs.pop();
    }

    // Execute the job.
    job.function();
  }
}

