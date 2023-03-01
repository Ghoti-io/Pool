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
#include <set>
#include <vector>
#include "pool.hpp"

using namespace std;
using namespace Ghoti::Pool;

namespace Ghoti::Pool {

/**
 * Container to hold the instance of the thread as well as a collection of
 * promises that must be fulfilled when the thread terminates.
 */
using ThreadInfo = pair<function<void()>, vector<promise<void>>>;

/**
 * The singleton reference to the global thread pool thread.
 */
static jthread globalPool{};

/**
 * Protects access to the control structures of the global thread pool.
 */
static auto globalMutex = make_shared<mutex>();

/**
 * Used to signal to the global thread pool that there is some action waiting
 * to be performed.
 */
static auto globalThreadSemaphore = make_shared<binary_semaphore>(0);

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
 * Job queue for threads that need to be created.
 */
static auto globalThreadCreateQueue = make_shared<queue<pair<promise<thread::id>, ThreadFunction>>>();

/**
 * Job queue of thread ids that need to be joined.
 */
static auto globalThreadJoinQueue = make_shared<queue<thread::id>>();

/**
 * Job queue of thread ids that need to be stopped.
 */
static auto globalThreadStopQueue = make_shared<queue<thread::id>>();

/**
 * Control structure used by the global thread pool to track threads and their
 * associated metadata.
 */
static auto threads = make_shared<map<thread::id, ThreadInfo>>();

/**
 * Control structure to determine whether or not the global thread pool thread
 * has been started (or needs to be started).
 */
static auto globalPoolNotStarted = make_shared<binary_semaphore>(1);


static void globalPoolLoop() {
  auto globalThreadSemaphore = Ghoti::Pool::globalThreadSemaphore;
  auto globalMutex = Ghoti::Pool::globalMutex;
  auto globalThreadCreateQueue = Ghoti::Pool::globalThreadCreateQueue;
  auto globalThreadStopQueue = Ghoti::Pool::globalThreadStopQueue;
  auto globalThreadJoinQueue = Ghoti::Pool::globalThreadJoinQueue;
  auto threads = Ghoti::Pool::threads;

  // Continue looping until a `break` condition happens.
  // The loop breaks when there is nothing queued up and no threads left
  // running.  When this loop breaks, the global thread pool queue will
  // terminate.  A new thread will be automatically created, however, when
  // any request is made for a new thread.
  while (true) {
    // Wait until there is work to do.
    globalThreadSemaphore->acquire();

    // Prevent race conditions.
    scoped_lock lock{*globalMutex};

    // Create a thread.
    while (!globalThreadCreateQueue->empty()) {
      auto [notifier, threadFunction] = move(globalThreadCreateQueue->front());
      globalThreadCreateQueue->pop();

      // Create the thread and put it into the control structure.
      jthread thread{threadFunction};
      auto threadId = thread.get_id();
      thread.detach();
      (*threads)[threadId] = ThreadInfo{{}, vector<promise<void>>{}};

      // Set the promise return value with the thread id that was created.
      notifier.set_value(threadId);
    }
    // Stop a thread.
    while (!globalThreadStopQueue->empty()) {
      auto threadId = globalThreadStopQueue->front();
      globalThreadStopQueue->pop();

      // Tell the thread to stop.
      if (threads->count(threadId) && (*threads)[threadId].first) {
        (*threads)[threadId].first();
      }
    }
    // Join a terminated thread.
    while (!globalThreadJoinQueue->empty()) {
      auto threadId = globalThreadJoinQueue->front();
      globalThreadJoinQueue->pop();

      // Look for the thread so that it can be joined.
      if (threads->count(threadId)) {
        // Make appropriate notifications.
        for (auto & notifier : (*threads)[threadId].second) {
          notifier.set_value();
        }

        // Clean up.
        // The thread will automatically join when it is erased here.
        threads->erase(threadId);
      }
    }

    // globalMutex is still acquired, which means that nothing else has been
    // able to write to our queues.  If they are all empty, and there are no
    // threads to be tracked, then terminate the global thread pool.  Another
    // one will be started if required by future thread requests.
    if (globalThreadCreateQueue->empty()
        && globalThreadStopQueue->empty()
        && globalThreadJoinQueue->empty()
        && threads->empty()) {
      // It is possible that there are many more signals waiting, so clean out
      // that semaphore.
      while (globalThreadSemaphore->try_acquire()) {}

      // Allow another pool to start, if required.
      globalPoolNotStarted->release();

      // Exit this thread.
      break;
    }

    // globalMutex is released.
  }

  // globalPool thread is exiting.  The thread will destroy itself, but since
  // it was detached, no other cleanup needs to be done.
}


thread::id createThread(ThreadFunction func) {
  // Create the promise that will be passed to the globalPool.
  promise<thread::id> notifier;
  auto notifierResult = notifier.get_future();

  {
    // Prevent race conditions.
    scoped_lock lock{*globalMutex};

    // Start a global thread pool (if it does not already exist).
    if (globalPoolNotStarted->try_acquire()) {
      globalPool = jthread{globalPoolLoop};
    }

    // Put the promise on the queue.
    globalThreadCreateQueue->emplace(move(notifier), move(func));

    // Let the globalPool know that there is work to do.
    globalThreadSemaphore->release();
  }

  // Block until the thread id is returned.
  return notifierResult.get();
}


static vector<future<void>> joinThreads(const set<thread::id> & threadIds) {
  vector<future<void>> notifierResults{};

  {
    // Prevent race conditions.
    scoped_lock lock{*globalMutex};

    // First, tell all the threads to stop.
    for (auto & threadId : threadIds) {

      // Verify that there is a thread to join.
      // The processing loop will make this check as well, but we must also
      // check here so that we can add the notifier.
      if (!threads->count(threadId)) {
        continue;
      }

      // Create the promise that will be passed to the correct globalPool.
      promise<void> notifier;

      // Save the notifier.
      notifierResults.push_back(notifier.get_future());

      // Signal for the thread to stop.
      globalThreadStopQueue->emplace(threadId);

      // Add the promise to the thread's notification list for when it finishes.
      (*threads)[threadId].second.push_back(move(notifier));
    }
  }

  // Let the globalPool know that there is work to do.
  globalThreadSemaphore->release();

  // All of the joins are completed.
  return notifierResults;
}


void joinGlobalPool() {
  // Get a list of all threads and join them.
  vector<future<void>> notifierResults{};

  {
    // Prevent race conditions.
    scoped_lock lock{*globalMutex};

    // Tell all the threads to stop, and set a join notification.
    for (auto & [threadId, threadInfo] : *threads) {

      // Create the promise that will be passed to the correct thread.
      promise<void> notifier;

      // Save the notifier.
      notifierResults.push_back(notifier.get_future());

      // Signal for the thread to stop.
      globalThreadStopQueue->emplace(threadId);

      // Add the promise to the thread's notification list for when it finishes.
      (*threads)[threadId].second.push_back(move(notifier));
    }
  }

  // Let the globalPool know that there is work to do.
  globalThreadSemaphore->release();

  // Wait for the joins.
  for (auto & notifier : notifierResults) {
    notifier.get();
  }
}


static bool stopThreads(const set<thread::id> & threadIds) {
  // Prevent race conditions.
  scoped_lock lock{*globalMutex};

  for (auto & threadId : threadIds) {
    // Signal for the thread to stop.
    globalThreadStopQueue->emplace(threadId);
  }

  // Let the globalPool know that there is work to do.
  globalThreadSemaphore->release();

  // Return immediately.
  return true;
}


size_t getGlobalPoolThreadCount() {
  // Prevent race conditions.
  scoped_lock lock{*globalMutex};

  return threads->size();
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
  set<thread::id> threads;

  /**
   * Track the waiting state of each thread.
   */
  map<thread::id, bool> threadsWaiting;

  /**
   * Track the threads that have been terminated.
   */
  set<thread::id> threadsTerminated;

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
  // Set the target thread count.
  {
    scoped_lock controlMutexLock{state->controlMutex};
    this->state->targetThreadCount = threadCount;
  }

  // Create new threads if needed.
  this->createThreads();

  // Notify threads so that they can remove themselves (or take something from
  // the queue) if needed.
  this->state->mutexCondition.notify_all();
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
  return this->state->threadsTerminated.size();
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
    this->state->threads.insert(createThread([state = this->state](stop_token token) -> void {
      return Ghoti::Pool::threadLoop(token, state);
    }));
  }
}


static void Ghoti::Pool::threadLoop(stop_token token, shared_ptr<State> state) {
  auto threadId = this_thread::get_id();

  // The thread loop will continue forever unless the terminate flag is set.
  while (true) {
    Job job;
    // Set our waiting state the true.
    {
      scoped_lock controlMutexLock{state->controlMutex};
      state->threadsWaiting[threadId] = true;
    }

    // Try to claim a Job.
    {
      unique_lock<mutex> queueMutexLock{state->queueMutex};

      // Wake up if there is a job or if the terminate flag is set.
      state->mutexCondition.wait(queueMutexLock, [&] {
        scoped_lock controlMutexLock{state->controlMutex};
        return state->terminate
            || (state->threads.size() > state->targetThreadCount)
            || token.stop_requested()
            || !state->jobs.empty();
      });

      {
        scoped_lock controlMutexLock{state->controlMutex};

        // Terminate the thread if needed.
        if (state->terminate
            || (state->threads.size() > state->targetThreadCount)
            || token.stop_requested()) {
          state->threads.erase(threadId);
          state->threadsWaiting.erase(threadId);
          state->threadsTerminated.insert(threadId);
          break;
        }

        // Tell the pool that we are no longer waiting.
        state->threadsWaiting[threadId] = false;
      }

      // Claim a job.
      job = move(state->jobs.front());
      state->jobs.pop();
    }

    // Execute the job.
    job.function();
  }

  {
    // Record that this thread is terminating.
    scoped_lock lock{*globalMutex};
    globalThreadJoinQueue->emplace(threadId);

    // Let the globalPool know that there is work to do.
    globalThreadSemaphore->release();
  }
}

