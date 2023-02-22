/**
 * @file
 * Header file supplied for use by 3rd party code so that they can easily
 * include all necessary headers.
 */

#ifndef AQUARIUM_HPP
#define AQUARIUM_HPP

#include <functional>
#include <memory>

namespace Ghoti::Pool{
// Forward declaration.
class State;

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
  /**
   * Default thread pool constructor.
   *
   * Will create as many threads as the total number of logical cores on the
   * system.
   */
  Pool();

  /**
   * Thread pool constructor for a specific number of cores.
   */
  Pool(size_t thread_count);

  /**
   * Thread pool destructor.
   *
   * When the pool is destroyed, it will signal all threads to stop.
   */
  ~Pool();

  // Remove the copy constructor.
  Pool(const Ghoti::Pool::Pool&) = delete;

  // Remove the copy assignment.
  Ghoti::Pool::Pool& operator=(const Ghoti::Pool::Pool&) = delete;

  /**
   * Enqueue a Job for the thread pool.
   *
   * @param job A rvalue representing the Job to be enqueued.
   * @returns True on success, False on failure.
   */
  bool enqueue(Job && job);

  /**
   * Start the thread pool processing.
   *
   * Will create threads as needed.
   */
  void start();

  /**
   * Stop the thread pool from dispatching new jobs and remove the existing
   * threads.
   *
   * Note: This will not halt any currently processing thread.  It will only
   * keep that thread from accepting a new Job from the queue.
   */
  void stop();

  /**
   * Stop the thread pool (if not already stopped) and join all threads.
   */
  void join();

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
   * Keep creating threads until the limit is reached.
   */
  void createThreads();

  /**
   * Common function loop for use by all threads in the thread pool.
   */
  static void threadLoop(std::shared_ptr<State>);

  /**
   * Pointer to the shared state of the thread pool.
   */
  std::shared_ptr<State> state;
};

};


#endif // AQUARIUM_HPP
