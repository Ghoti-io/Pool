/**
 * @file
 *
 * Code for the Pool thread pool.
 */

#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <queue>
#include <semaphore>
#include <vector>
#include "pool.hpp"
#include <iostream>

using namespace std;
using namespace Ghoti::Pool;

namespace Ghoti::Pool {

/**
 * Container to hold the instance of the thread as well as a collection of
 * promises that must be fulfilled when the thread terminates.
 */
using ThreadInfo = pair<jthread, vector<promise<void>>>;

/**
 * Protects access to the control structures of the global thread pool.
 */
static mutex globalMutex;

/**
 * Used to signal to the global thread pool that there is some action waiting
 * to be performed.
 */
static counting_semaphore globalThreadSemaphore{0};

/**
 * Common function loop for use by all threads in the thread pool.
 *
 * @param token Token indicating that this jthread has been asked to stop.
 * @param state The shared pool state.
 */
static void threadLoop(stop_token token, shared_ptr<State> state);

/**
 * The thread of execution used by the global thread pool, from which all other
 * threads are created and controlled.
 *
 * This thread will be responsible for creating and orchestrating all other
 * thread control processes.
 *
 * The reason for this approach is that the Pool class is designed so that, if
 * the Pool is destroyed, all of its threads will not also be instantly
 * destroyed, but that they will each be signaled to shut down, so that each
 * thread can perform the proper cleanup.  By that time, however, the Pool
 * itself may be destroyed, and there must be some parent thread to perform the
 * cleanup.  That need for cleanup is what this Global thread pool performs.
 */
static void globalPoolLoop();

/**
 * Function that will ask the global thread pool to create an additional thread.
 *
 * @param state The shared state that will be supplied to the thread.
 * @return The id of the thread that was created.
 */
static thread::id createThread(shared_ptr<State> state);

/**
 * Job queue for threads that need to be created.
 */
static queue<pair<promise<thread::id>, shared_ptr<State>>> globalThreadCreateQueue;

/**
 * Job queue of thread ids that need to be joined.
 */
static queue<thread::id> globalThreadJoinQueue;

/**
 * Job queue of thread ids that need to be stopped.
 */
static queue<thread::id> globalThreadStopQueue;

/**
 * Control structure used by the global thread pool to track threads and their
 * associated metadata.
 */
static map<thread::id, ThreadInfo> threads;

/**
 * Control structure to determine whether or not the global thread pool thread
 * has been started (or needs to be started).
 */
binary_semaphore globalPoolNotStarted{1};


static void globalPoolLoop() {
  // Continue looping until a `break` condition happens.
  // The loop breaks when there is nothing queued up and no threads left
  // running.  When this loop breaks, the global thread pool queue will
  // terminate.  A new thread will be automatically created, however, when
  // any request is made for a new thread.
  while (true) {
    // Wait until there is work to do.
    globalThreadSemaphore.try_acquire_for(50ms);

    // Prevent race conditions.
    scoped_lock lock{globalMutex};

    // Create a thread.
    while (!globalThreadCreateQueue.empty()) {
      auto [notifier, state] = move(globalThreadCreateQueue.front());
      globalThreadCreateQueue.pop();

      // Create the thread and put it into the control structure.
      jthread thread{threadLoop, state};
      auto threadId = thread.get_id();
      threads[threadId] = ThreadInfo{move(thread), vector<promise<void>>{}};

      // Set the promise return value with the thread id that was created.
      notifier.set_value(threadId);
    }
    // Stop a thread.
    while (!globalThreadStopQueue.empty()) {
      auto threadId = globalThreadStopQueue.front();
      globalThreadStopQueue.pop();

      // Tell the thread to stop.
      if (threads.count(threadId)) {
        threads[threadId].first.request_stop();
      }
    }
    // Join a terminated thread.
    while (!globalThreadJoinQueue.empty()) {
      auto threadId = globalThreadJoinQueue.front();
      globalThreadJoinQueue.pop();

      // Look for the thread so that it can be joined.
      if (threads.count(threadId)) {
        auto & [thread, notifiers] = threads[threadId];

        // Make appropriate notifications.
        for (auto & notifier : notifiers) {
          notifier.set_value();
        }

        // Clean up.
        // The thread will automatically join when it is erased here.
        threads.erase(threadId);
      }
    }

    // globalMutex is still acquired, which means that nothing else has been
    // able to write to our queues.  If they are all empty, and there are no
    // threads to be tracked, then terminate the global thread pool.  Another
    // one will be started if required by future thread requests.
    if (globalThreadCreateQueue.empty()
        && globalThreadStopQueue.empty()
        && globalThreadJoinQueue.empty()
        && threads.empty()) {
      // It is possible that there are many more signals waiting, so clean out
      // that semaphore.
      while (globalThreadSemaphore.try_acquire()) {}

      // Allow another pool to start, if required.
      globalPoolNotStarted.release();

      // Exit this thread.
      break;
    }

    // globalMutex is released.
  }

  // globalPool thread is exiting.  The thread will destroy itself, but since
  // it was detached, no other cleanup needs to be done.
}


static thread::id createThread(shared_ptr<State> state) {
  // Create the promise that will be passed to the globalPool.
  promise<thread::id> notifier;
  auto notifierResult = notifier.get_future();

  {
    // Prevent race conditions.
    scoped_lock lock{globalMutex};

    // Start a global thread pool (if it does not already exist).
    if (globalPoolNotStarted.try_acquire()) {
      jthread{globalPoolLoop}.detach();
    }

    // Put the promise on the queue.
    globalThreadCreateQueue.emplace(move(notifier), state);

    // Let the globalPool know that there is work to do.
    globalThreadSemaphore.release();
  }

  // Block until the thread id is returned.
  return notifierResult.get();
}


static vector<future<void>> joinThreads(const vector<thread::id> & threadIds) {
  vector<future<void>> notifierResults{};

  {
    // Prevent race conditions.
    scoped_lock lock{globalMutex};

    // First, tell all the threads to stop.
    for (auto & threadId : threadIds) {

      // Verify that there is a thread to join.
      // The processing loop will make this check as well, but we must also
      // check here so that we can add the notifier.
      if (!threads.count(threadId)) {
        continue;
      }

      // Create the promise that will be passed to the correct globalPool.
      promise<void> notifier;

      // Save the notifier.
      notifierResults.push_back(notifier.get_future());

      // Signal for the thread to stop.
      globalThreadStopQueue.emplace(threadId);

      // Add the promise to the thread's notification list for when it finishes.
      threads[threadId].second.push_back(move(notifier));
    }
  }

  // Let the globalPool know that there is work to do.
  globalThreadSemaphore.release();

  // All of the joins are completed.
  return notifierResults;
}


void joinGlobalPool() {
  // Get a list of all threads and join them.
  vector<future<void>> notifierResults{};

  {
    // Prevent race conditions.
    scoped_lock lock{globalMutex};

    // Tell all the threads to stop, and set a join notification.
    for (auto & [threadId, threadInfo] : threads) {

      // Create the promise that will be passed to the correct thread.
      promise<void> notifier;

      // Save the notifier.
      notifierResults.push_back(notifier.get_future());

      // Signal for the thread to stop.
      globalThreadStopQueue.emplace(threadId);

      // Add the promise to the thread's notification list for when it finishes.
      threads[threadId].second.push_back(move(notifier));
    }
  }

  // Let the globalPool know that there is work to do.
  globalThreadSemaphore.release();

  // Wait for the joins.
  for (auto & notifier : notifierResults) {
    notifier.get();
  }
}


static bool stopThreads(const vector<thread::id> & threadIds) {
  // Prevent race conditions.
  scoped_lock lock{globalMutex};

  for (auto & threadId : threadIds) {
    // Signal for the thread to stop.
    globalThreadStopQueue.emplace(threadId);
  }

  // Let the globalPool know that there is work to do.
  globalThreadSemaphore.release();

  // Return immediately.
  return true;
}


size_t getGlobalPoolThreadCount() {
  // Prevent race conditions.
  scoped_lock lock{globalMutex};

  return threads.size();
}

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
  vector<thread::id> threads;

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

  ~State() {}
};
}


Pool::Pool() : Pool(thread::hardware_concurrency()) {}


Pool::Pool(size_t threadCount) {
  this->state = make_shared<State>();
  this->state->terminate = true;
  this->state->targetThreadCount = threadCount;
}


Pool::~Pool() {
  this->stop();
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
    this->state->terminate = true;

    stopThreads(this->state->threads);
  }

  // Wake up all threads so that they will terminate themselves.
  this->state->mutexCondition.notify_all();
}


void Pool::join() {
  // Join the threads.
  auto notifierResults = joinThreads(this->state->threads);

  // Clean up the threads from the state object.
  {
    scoped_lock<mutex> controlMutexLock{state->controlMutex};
    this->state->terminate = true;
    this->state->threads.clear();
  }

  // Wake up all threads so that they will terminate themselves.
  this->state->mutexCondition.notify_all();

  // Now try to join the threads.
  for (auto & notifierResult : notifierResults) {
    // Wait for the thread to join in the globalPool thread.
    notifierResult.get();
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
  scoped_lock controlMutexLock{this->state->controlMutex};

  // Create threads in the pool.
  while (this->state->threads.size() < this->state->targetThreadCount) {
    this->state->threads.push_back(createThread(this->state));
  }
}


static void Ghoti::Pool::threadLoop(stop_token token, shared_ptr<State> state) {
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
        return !state->jobs.empty() || state->terminate;
      });

      {
        scoped_lock controlMutexLock{state->controlMutex};

        // Terminate the thread if needed.
        if (state->terminate
            || (state->threads.size() > state->targetThreadCount)
            || token.stop_requested()) {
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

  {
    // Record that this thread is terminating.
    scoped_lock lock{globalMutex};
    globalThreadJoinQueue.emplace(thread_id);

    // Let the globalPool know that there is work to do.
    globalThreadSemaphore.release();
  }
}

