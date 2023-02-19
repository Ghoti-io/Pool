/**
 * @file
 * Header file supplied for use by 3rd party code so that they can easily
 * include all necessary headers.
 */

#ifndef AQUARIUM_HPP
#define AQUARIUM_HPP

#include<condition_variable>
#include<functional>
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
  void stop();
  bool hasJobsWaiting();

  private:
  /**
   * Common function loop for use by all threads in the thread pool.
   */
  void threadLoop();

  /**
   * Set of available threads.
   */
  std::vector<std::jthread> threads;

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
};

};


#endif // AQUARIUM_HPP