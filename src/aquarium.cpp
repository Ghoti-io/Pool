/**
 * @file
 *
 * Code for the Pool thread pool.
 */

#include "aquarium.hpp"
#include <iostream>

using namespace std;
using namespace Ghoti::Pool;

Pool::Pool() : terminate{false} {
  // Get the number of logical cores.
  auto thread_start = thread::hardware_concurrency();

  // Create threads in the pool.
  this->threads.reserve(thread_start);
  for (size_t i = 0; i < thread_start; ++i) {
    this->threads.emplace_back(&Pool::Pool::threadLoop, this);
  }
}

void Pool::threadLoop() {
  // The thread loop will continue forever unless the terminate flag is set.
  while (true) {
    Job job;
    {
      unique_lock<mutex> lock{this->queueMutex};

      // Wake up if there is a job or if the terminate flag is set.
      this->mutexCondition.wait(lock, [this] {
        return !this->jobs.empty() || this->terminate;
      });

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

bool Pool::enqueue(Job && job) {
  {
    unique_lock<mutex> lock{this->queueMutex};

    // Do not enqueue the job if the pool has been set to terminate.
    if (this->terminate) {
      return false;
    }

    this->jobs.emplace(move(job));
  }

  // Notify a thread that a job is available.
  this->mutexCondition.notify_one();
  return true;
}

bool Pool::hasJobsWaiting() {
  unique_lock<mutex> lock{this->queueMutex};
  return this->jobs.empty();
}

void Pool::stop() {
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

