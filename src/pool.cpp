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
  mutex controlMutex;

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
  size_t targetThreadCount;
};
}

Pool::Pool() : Pool(thread::hardware_concurrency()) {}

Pool::Pool(size_t threadCount) {
  this->state = make_shared<State>();
  this->state->terminate = true;
  this->state->targetThreadCount = threadCount;
}

Pool::~Pool() {
  // Causes resource lockup when not ->join().  why?????
  this->join();
}

bool Pool::enqueue(Job && job) {
  scoped_lock locks{this->state->queueMutex, this->state->controlMutex};

  this->state->jobs.emplace(move(job));

  // Notify a thread that a job is available.
  if (!this->state->terminate) {
    this->state->mutexCondition.notify_one();
  }
  return true;
}

void Pool::start() {
  // Don't try to start the pool if it is already running.
  {
    scoped_lock locks{this->state->queueMutex, this->state->controlMutex};
    if (!this->state->terminate) {
      return;
    }
    // Set the stop condition.
    this->state->terminate = false;
  }

  this->createThreads();
}

void Pool::stop() {
  // Set the stop condition.
  {
    scoped_lock locks{this->state->queueMutex, this->state->controlMutex};

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

  {
    //Causes lockup... why???
    //scoped_lock<mutex> controlMutexLock{state->controlMutex};

    // Join the threads.
    for (auto & thread : this->state->threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }

    // Clean up the threads.
    this->state->threads.clear();
  }
}

size_t Pool::getJobQueueCount() {
  scoped_lock queueMutexLock{this->state->queueMutex};
  return this->state->jobs.size();
}

void Pool::setThreadCount(size_t threadCount) {
  {
    scoped_lock controlMutexLock{state->controlMutex};
    this->state->targetThreadCount = threadCount;
  }
  this->createThreads();
}

size_t Pool::getThreadCount() const {
  scoped_lock controlMutexLock{state->controlMutex};
  return this->state->threads.size();
}

size_t Pool::getWaitingThreadCount() const {
  scoped_lock controlMutexLock{state->controlMutex};
  size_t count{0};

  for (auto & [thread_id, waiting] : state->threadsWaiting) {
    if (waiting) {
      count++;
    }
  }

  return count;
}

size_t Pool::getTerminatedThreadCount() const {
  scoped_lock controlMutexLock{state->controlMutex};
  return this->state->threads.size() - this->state->threadsWaiting.size();
}

size_t Pool::getRunningThreadCount() const {
  scoped_lock controlMutexLock{state->controlMutex};
  size_t count{0};

  for (auto & [thread_id, waiting] : state->threadsWaiting) {
    if (!waiting) {
      count++;
    }
  }

  return count;
}

void Pool::createThreads() {
  // Create threads in the pool.
  scoped_lock controlMutexLock{this->state->controlMutex};
  if (this->state->threads.size() < this->state->targetThreadCount) {
    this->state->threads.reserve(this->state->targetThreadCount);
  }

  while (this->state->threads.size() < this->state->targetThreadCount) {
    this->state->threads.emplace_back(jthread{&Pool::Pool::threadLoop, this->state});
  }
}

void Pool::threadLoop(shared_ptr<State> state) {
  auto thread_id = this_thread::get_id();

  // The thread loop will continue forever unless the terminate flag is set.
  while (true) {
    Job job;
    // Set our waiting state the true.
    {
      scoped_lock controlMutexLock{state->controlMutex};
      state->threadsWaiting[thread_id] = true;
    }

    // Try to claim a Job.
    {
      unique_lock<mutex> queueMutexLock{state->queueMutex};

      // Wake up if there is a job or if the terminate flag is set.
      state->mutexCondition.wait(queueMutexLock, [&] {
        scoped_lock controlMutexLock{state->controlMutex};
        return !state->jobs.empty() || state->terminate;
      });

      {
        scoped_lock controlMutexLock{state->controlMutex};

        // Terminate the thread if needed.
        if (state->terminate
            || (state->threads.size() > state->targetThreadCount)) {
          break;
        }

        // Tell the pool that we are no longer waiting.
        state->threadsWaiting[thread_id] = false;
      }

      // Claim a job.
      job = move(state->jobs.front());
      state->jobs.pop();
    }

    // Execute the job.
    job.function();

    {
      scoped_lock controlMutexLock{state->controlMutex};

      // Terminate this thread if there are too many threads in the pool.
      if (state->threads.size() > state->targetThreadCount) {
        break;
      }
    }
  }

  // Remove thread_id from the waiting structure.
  {
    scoped_lock controlMutexLock{state->controlMutex};
    state->threadsWaiting.erase(thread_id);
  }
}

