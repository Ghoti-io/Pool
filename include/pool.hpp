/**
 * @file
 * Header file supplied for use by 3rd party code so that they can easily
 * include all necessary headers.
 */

#ifndef AQUARIUM_HPP
#define AQUARIUM_HPP

#include<condition_variable>
#include<functional>
#include<map>
#include<memory>
#include<mutex>
#include<thread>
#include<queue>
#include<vector>

namespace Ghoti::Pool{
/**
 * Holds information about a job.
 */
struct Job {
  std::function<void()> function;
};

/**
 * Represents a generalized thread pool.
 */
class Pool {
  public:
  Pool();

  // Remove the copy constructor.
  Pool(const Ghoti::Pool::Pool&) = delete;

  // Remove the copy assignment.
  Ghoti::Pool::Pool& operator=(const Ghoti::Pool::Pool&) = delete;

  bool enqueue(Job && job);
  void start();
  void stop();

  /**
   * Returns the number of jobs currently in the job queue.
   *
   * @returns The number of jobs currently in the job queue.
   */
  size_t getJobQueueCount();

  /**
   * Returns the number of threads that are created.
   *
   * @returns The number of threads that are created.
   */
  size_t getThreadCount() const;

  /**
   * Returns the number of threads that are waiting.
   *
   * @returns The number of threads that are waiting.
   */
  size_t getWaitingThreadCount() const;

  /**
   * Returns the number of threads that are terminated.
   *
   * @returns The number of threads that are terminated.
   */
  size_t getTerminatedThreadCount() const;

  /**
   * Returns the number of threads that are running.
   *
   * @returns The number of threads that are running.
   */
  size_t getRunningThreadCount() const;

  private:
  /**
   * Common function loop for use by all threads in the thread pool.
   */
  void threadLoop();

  /**
   * Collection of available threads.
   */
  std::vector<std::jthread> threads;

  /**
   * Track the waiting state of each thread.
   */
  std::map<std::thread::id, bool> threadsWaiting;

  /**
   * Queue of jobs waiting to be assigned to a thread.
   */
  std::queue<Job> jobs;

  /**
   * Mutex to control access to the Queue.
   */
  std::mutex queueMutex;

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

};


#endif // AQUARIUM_HPP
