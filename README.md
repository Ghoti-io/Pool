# Ghoti.io Pool
Ghoti.io (*pronounced [fish](https://en.wikipedia.org/wiki/Ghoti)*) Pool is a dynamically-linked, C++ thread pool, task queue library.

This libraries allow thread pools to be created and tasks to be assigned to the pools.  The features that make this library worth using are:
 - **Thread pool control is automatic,** without the need to add boilerplate to the beginning and ending of your main() function.
 - **Thread pools can be increased/decreased** even after they have been started.
 - **Multiple thread pools.**  This allows you to tune the number of threads to the task needed.  Put CPU-intensive tasks into one pool and IO-intensive tasks into another.
 - **Thread safety.** The `Pool` object is thread safe.

## Example Code
This is what I plan for the code behavior to be.  The `#include` might be
different, depending on how you compile this project.

```C++
#include <Ghoti.io/pool.hpp>

// For the thread sleep...
#include <thread>
#include <iostream>

using namespace std;

/**
 * Cause a thread pool with two worker threads to be created.  Enqueue 3 tasks
 * and let the thread pool begin working on the tasks.  Exit the main thread,
 * which will gracefully shut down the thread pool.
 *
 * This example requires that the system can run multithreaded processes.
 */
int main() {
  // Declare a pool of 2 worker threads.
  Ghoti::Pool::Pool a{2};

  cout << "Starting the threads." << endl;

  // The worker threads for the thread pool will be created.
  a.start();

  // Task 1
  a.enqueue({[](){
    this_thread::sleep_for(500ms);
    cout << "First task finished!" << endl;
  }});

  // Task 2
  a.enqueue({[](){
    this_thread::sleep_for(250ms);
    cout << "Second task finished!" << endl;
  }});

  // Task 3
  // Note: This task will never start, because the thread pool will be stopped
  // before the task has time to start, since we only declared 2 threads in this
  // pool.
  a.enqueue({[](){
    this_thread::sleep_for(250ms);
    cout << "Third task finished!" << endl;
  }});

  // Make the main thread sleep.
  // This is only necessary because this is a contrived example and the main()
  // function will exit very, very soon.  So soon, in fact, that it will
  // probably happen before the worker threads have time to wake up and claim a
  // task.
  //
  // By putting the main thread to sleep, we are providing time for the 2
  // worker threads to claim tasks from the thread pool, as they would be doing
  // anyway.
  //
  // This serves to show that, when the the main() function exits and destroys
  // the thread pool object (`a`), the unjoined workers are not immedately
  // terminated, but rather they finish their task and gracefully exit, and the
  // process itself will not end until the worker threads terminate.
  this_thread::sleep_for(1ms);

  cout << "At the end of main.  What will happen with the threads?" << endl;

  // At this point, `a` will pass out of scope.  The task queue will be
  // discarded, and the threads that are running in the thread queue will be
  // signaled to stop.
  //
  // The worker threads will then exit after completing their currently
  // assigned task, and the process will close safely.
  //
  // Traditional thread behavior would simply (and without regard to what the
  // threads were doing) exit without giving the threads a chance to shut down
  // gracefully.
}
```
### Sample Output
```
Starting the threads.
At the end of main.  What will happen with the threads?
Second task finished!
First task finished!
```

## Thread Pool API

### Constructor
```C++
// Defaults the number of threads to the number of logical processors on the
// system.
Ghoti::Pool::Pool threadpool{};

// Thread pool of 5 worker threads.
Ghoti::Pool::Pool threadpool{5};
```

### Starting the pool
The thread pool does not start automatically.
```C++
Ghoti::Pool::Pool threadpool{5};
threadpool.start();
```

### Adding a task to the pool
The `Ghoti::Pool::Task` object has two parts:
 1. A function that will execute the desired task.
 2. An optional function which, if provided, will be called when the pool is
    being stopped.  It is a way for you to signal to your thread (if it is
    otherwise running in an infinite loop, for example).

Call the `Ghoti::Pool::Pool::enqueue()` function to add a `Task` to the queue.

 - Tasks may be enqueued at any time.
 - Tasks may be enqueued from any thread.
 - Enqueueing a task does not start the thread pool.
 - The task queue is deleted when the pool itself passes out of scope and the
   last active worker thread in that thread pool returns.

```C++
// A task with only the task function.
Ghoti::Pool::Task t1{
  [](){
    // Task 1.
    // Do something.
  }
};

Ghoti::Pool::Task t2{
  [](){
    // Task 2.
    // Do something.
  },
  [](){
    // We need to stop!
    // Do something to indicate to task 2 that it should return!
  }
}

Ghoti::Pool::Pool threadpool{5};

threadpool.enqueue(t1);
threadpool.enqueue(t2);
```
### Stopping the pool
The thread pool can be stopped by either its `.stop()` or its `.join()` method.

#### The Asynchronous `.stop()`
`Ghoti::Pool::Pool::stop()` will signal for all worker threads to stop, but it
does not wait for the worker threads to stop.  It is asynchronous.

When a thread pool is destroyed, it calls `.stop()` automatically.

#### The synchronous `.join()`
`Ghoti::Pool::Pool::join()` will signal for all worker threads to stop, but it
then block until all worker threads have exited.

## Motivation (i.e., Why would I write this?)
Threads are neither complicated nor trivial, but they are nuanced.  In
traditional thread behavior, one thread (A) creates another, child thread (B).
If A is the main thread and it ends, the process won't end until B terminates.
Unless B is a detached thread, and then it just aborts.  Of course, you could
simply add code to A that will tell B to exit, but that requires the programmer
to put the code in the correct place.  And, you have to worry that, if you join
thread B, that you join it into A and not some other thread, otherwise you have
an entirely different set of problems.

*Yeah, that paragraph was boring and filled with caveats and edge cases.*

Modern C++ has better constructs for dealing with threads, and if we design
our library correctly, we can elimiate the fiddly bits of boilerplate and
needing to become a synchronization expert just to have a task queue in our
projects.

That is where `Ghoti.io::Pool` comes in.

This project will manage the worker pools and task queues for you so that you
can focus on the parts of your project that you enjoy.  It's a utility, pure
and simple.

## Design
This library operates in the following way:
  1. It uses a **managing thread** to create and join all **worker threads**.
  1. The managing thread is not created until worker threads are needed, and
     the managing thread exits when all of the worker threads have exited.
  1. If the managing thread has exited, but more worker threads are needed,
     then a new managing thread will be created automatically.
  1. The managing thread is global to the process, so controlling it (asking
     for new workers, stopping workers, joining workers) must be available to
     the entire process.
  1. Interacting with the managing thread must be, in itself, thread safe, so
     the library should do the dirty work there.
  1. The control structures for the managing threads are global (but static)
     shared pointers.  As such, they are instantiated before the `main()`
     thread begins, and are not removed until after `main()` exits.
  1. The managing thread also keeps a copy of the shared pointers to the
     control structures so that, in the event that the `main()` thread exits
     and the global static variables are destroyed (which is normal), the
     values that they pointed to will **not** be destroyed until the managing
     thread also exits.
  1. When a Pool object is destroyed, it automatically requests for all of its
     threads to be stopped.
  1. Stopping a pool is asynchronous, and the worker threads must be allowed
     to terminate themselves gracefully.
  1. The managing thread will terminate after all of the worker threads that it
     is managing have been stopped, joined, and it has no further requests in
     its queues.

The end effect is a thread-safe library that will automatically manage thread
lifecycles without the need to add boilerplate code.

