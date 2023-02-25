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
 * Function that must be called in order to terminate and join the Global
 * thread pool.
 *
 * The global thread pool must be ended before the program can terminate.  The
 * pool will terminate automatically, on its own, when all of its threads have
 * self-terminated.  It may be, however, that the threads do not know that they
 * need to terminate.
 *
 * This function will asynchronously request that all threads stop, and then
 * block until all threads join.
 *
 * The main program will not end until all threads have terminated.  It may not
 * be necessary to call this function explicitly, depending on the design of
 * your program.
 */
void joinGlobalPool();

/**
 * Get the total number of threads being tracked by the global thread pool.
 *
 * @returns The number of threads being tracked by the global thread pool.
 */
size_t getGlobalPoolThreadCount();

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
   * Thread pool constructor for a specific number of threads.
   *
   * @param threadCount The desired number of threads.
   */
  Pool(size_t threadCount);

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
   * Set the thread count.
   *
   * If the pool is not running, then no threads will be created.  If the pool
   * is running and the number specified is higher than the current pool size,
   * then new threads will be created.  If the number specified is lower than
   * the current pool size, then threads will be removed as they finish their
   * tasks.  Threads will not be interrupted.
   *
   * @param threadCount The desired thread count.
   */
  void setThreadCount(size_t threadCount);

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
   * Pointer to the shared state of the thread pool.
   */
  std::shared_ptr<State> state;
};

};


#endif // AQUARIUM_HPP
