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
 * Function that must be called in order to set up the Global thread pool.
 *
 * This function must be called from the main thread, and no subsequent threads
 * will be created until this function has been called.  It will create a
 * separate monitoring thread which will serve to orchestrate all other thread
 * control processes.
 *
 * The reason for this approach is that the Pool class is designed so that, if
 * the Pool is destroyed, all of its threads will not also be instantly
 * destroyed, but that they will each be signaled to shut down, so that each
 * thread can perform the proper cleanup.  By that time, however, the Pool
 * itself may be destroyed, and there must be some parent thread to perform the
 * cleanup.  That need for cleanup is what this Global thread pool performs.
 */
void startGlobalPool();

/**
 * Function that must be called in order to terminate and join the Global
 * thread pool.
 *
 * This function must be called before the main thread can terminate.  If it
 * is not called, then the program will not exit.
 */
void endGlobalPool();

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
   * Common function loop for use by all threads in the thread pool.
   */
  //static void threadLoop(std::shared_ptr<State>);

  /**
   * Pointer to the shared state of the thread pool.
   */
  std::shared_ptr<State> state;
};

};


#endif // AQUARIUM_HPP
