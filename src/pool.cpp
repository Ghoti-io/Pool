/**
 * @file
 *
 * Code for the Pool thread pool.
 */

#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <queue>
#include <vector>
#include "pool.hpp"

using namespace std;
using namespace Ghoti::Pool;

namespace Ghoti::Pool {

/**
 * Structure to hold the state of the pool.
 *
 * If the pool object is destroyed, then the threads must exit safely, but in
 * order to do so, the synchronization mutexes and queues must still exist.
 * To accomplish this, the State is provided to each thread as a shared
 * pointer.  As such, the State will be destroyed when the object pool and
 * all associated threads are destroyed.
 */
struct State {
  /**
   * Mutex to control access to the Queue.
   */
  mutex queueMutex;

  /**
   * Mutex to control access to the threadsWaiting map.
   */
  mutex threadsWaitingMutex;

  /**
   * Collection of available threads.
   */
  vector<std::jthread> threads;

  /**
   * Track the waiting state of each thread.
   */
  map<thread::id, bool> threadsWaiting;

  /**
   * Queue of jobs waiting to be assigned to a thread.
   */
  queue<Job> jobs;

  /**
   * Indicates whether or not the threads should terminate.
   */
  bool terminate;

  /**
   * Allows threads to wait on new jobs or termination.
   */
  std::condition_variable mutexCondition;

  /**
   * The number of threads that the pool should manage.
   *
   * This defaults to the number of logical cores on the system.
   */
  size_t thread_target_count;
};
}

Pool::Pool() : Pool(thread::hardware_concurrency()) {}

Pool::Pool(size_t thread_count) {
  this->state = make_shared<State>();
  this->state->terminate = true;
  this->state->thread_target_count = thread_count;
}

Pool::~Pool() {
  this->stop();
}

bool Pool::enqueue(Job && job) {
  {
    unique_lock<mutex> lock{this->state->queueMutex};
    this->state->jobs.emplace(move(job));
  }

  // Notify a thread that a job is available.
  if (!this->state->terminate) {
    this->state->mutexCondition.notify_one();
  }
  return true;
}

void Pool::start() {
  // Don't try to start the pool if it is already running.
  if (!this->state->terminate) {
    return;
  }

  // Set the stop condition.
  {
    unique_lock<mutex> lock{this->state->queueMutex};
    this->state->terminate = false;
  }

  // Create threads in the pool.
  this->state->threads.reserve(this->state->thread_target_count);

  for (size_t i = 0; i < this->state->thread_target_count; ++i) {
    jthread thread{&Pool::Pool::threadLoop, this->state};
    // Other threads may already be running, so use a mutex to protect access.
    {
      unique_lock<mutex> waitingLock{state->threadsWaitingMutex};
      this->state->threadsWaiting[thread.get_id()] = false;
    }
    this->state->threads.emplace_back(move(thread));
  }
}

void Pool::stop() {
  // Set the stop condition.
  {
    unique_lock<mutex> lock{this->state->queueMutex};

    // Don't try to stop an already-stopped pool.
    if (this->state->terminate) {
      return;
    }

    this->state->terminate = true;
  }

  // Wake up all threads so that they will terminate themselves.
  this->state->mutexCondition.notify_all();
}

void Pool::join() {
  // Stop the threads (if not already done).
  this->stop();

  // Join the threads.
  for (auto & thread : this->state->threads) {
    thread.join();
  }

  // Clean up the threads.
  this->state->threads.clear();
}

size_t Pool::getJobQueueCount() {
  unique_lock<mutex> lock{this->state->queueMutex};
  return this->state->jobs.size();
}

size_t Pool::getThreadCount() const {
  return this->state->threads.size();
}

size_t Pool::getWaitingThreadCount() const {
  size_t count{0};
  {
    unique_lock<mutex> waitingLock{state->threadsWaitingMutex};
    for (auto & [thread_id, waiting] : state->threadsWaiting) {
      if (waiting) {
        count++;
      }
    }
  }

  return count;
}

size_t Pool::getTerminatedThreadCount() const {
  unique_lock<mutex> waitingLock{state->threadsWaitingMutex};
  return this->state->threads.size() - this->state->threadsWaiting.size();
}

size_t Pool::getRunningThreadCount() const {
  size_t count{0};
  {
    unique_lock<mutex> waitingLock{state->threadsWaitingMutex};
    for (auto & [thread_id, waiting] : state->threadsWaiting) {
      if (!waiting) {
        count++;
      }
    }
  }

  return count;
}

void Pool::threadLoop(shared_ptr<State> state) {
  auto thread_id = this_thread::get_id();

  // The thread loop will continue forever unless the terminate flag is set.
  while (true) {
    Job job;
    // Set our waiting state the true.
    {
      unique_lock<mutex> waitingLock{state->threadsWaitingMutex};
      state->threadsWaiting[thread_id] = true;
    }

    // Try to claim a Job.
    {
      unique_lock<mutex> lock{state->queueMutex};

      // Wake up if there is a job or if the terminate flag is set.
      state->mutexCondition.wait(lock, [&] {
        return !state->jobs.empty() || state->terminate;
      });

      // Terminate the thread.
      if (state->terminate) {
        break;
      }

      {
        unique_lock<mutex> waitingLock{state->threadsWaitingMutex};
        // Tell the pool that we are no longer running.
        state->threadsWaiting[thread_id] = false;
      }

      // Claim a job.
      job = move(state->jobs.front());
      state->jobs.pop();
    }

    // Execute the job.
    job.function();
  }

  // Remove thread_id from the waiting structure.
  {
    unique_lock<mutex> waitingLock{state->threadsWaitingMutex};
    state->threadsWaiting.erase(thread_id);
  }
}

