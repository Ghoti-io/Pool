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

using namespace std;
using namespace Ghoti::Pool;

namespace Ghoti::Pool {

static void threadLoop(stop_token token, shared_ptr<State> state);
static void globalPoolLoop();
static thread::id createThread(shared_ptr<State> state);

using ThreadInfo = pair<jthread, vector<promise<void>>>;
static mutex globalMutex;
static counting_semaphore globalThreadSemaphore{0};

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

static map<thread::id, ThreadInfo> threads;
static jthread globalPool{};
static bool terminatePool;

void startGlobalPool() {
  if (!globalPool.joinable()) {
    scoped_lock lock{globalMutex};
    terminatePool = false;
    globalPool = jthread{globalPoolLoop};
  }
}

void endGlobalPool() {
  {
    scoped_lock lock{globalMutex};
    terminatePool = true;
    // Wake up the global pool and end it.
    globalThreadSemaphore.release();
  }
  globalPool.join();
}

static void globalPoolLoop() {
  terminatePool = false;

  while (!terminatePool) {
    // Wait until there is work to do.
    globalThreadSemaphore.acquire();

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
        if (thread.joinable()) {
          // Join the thread.
          thread.join();

          // Make appropriate notifications.
          for (auto & notifier : notifiers) {
            notifier.set_value();
          }

          // Clean up.
          threads.erase(threadId);
        }
      }
    }
  }
}

static thread::id createThread(shared_ptr<State> state) {
  // Create the promise that will be passed to the globalPool.
  promise<thread::id> notifier;
  auto notifierResult = notifier.get_future();
  {
    // Prevent race conditions.
    scoped_lock lock{globalMutex};

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
  if (this->state->threads.size() < this->state->targetThreadCount) {
    this->state->threads.reserve(this->state->targetThreadCount);
  }

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

