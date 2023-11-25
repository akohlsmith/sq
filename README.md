# simple queue

multiple producer, multiple consumer, uses pthreads for locking

does not try to get fancy with lockless design, just a simple queue

requires dynamic memory allocation - like I said, just a simple/basic queue.

Each queue is represented by a single `sq_t` struct. The queue contains a number of individual elements, each represented by an `sq_elem_t` struct. Add to the queue with `sq_push()`, remove from the queue with `sq_pop()`. If you're interested in waiting for data to be pushed to the queue, create a condition var and call `sq_add_listener()`. You can then use `pthread_cond_wait()` or `pthread_cond_timedwait()` and your thread will be awoken when new data is pushed.

Push data to multiple queues by using `sq_publish()`; it takes an `sq_list_t` of queues to `push()` to and a `sq_elem_t` that will be pushed to all queues on the list, each being entirely independent from its siblings.

Queue flags and element flags are described below, but some notes:

* `SQ_FLAG_VOLATILE` - if an element has this flag, it means that the data pointer will not
stick around. `push()` will allocate memory and copy the data to the new buffer, and it also
means that when you `pop()`, you must use/copy the data before you `free()` the element, because the data was allocated along with the element struct itself.

* `SQ_FLAG_FREE` - if a `pop()`'d element has this flag then the data pointer must be explicitly `free()`'d when you're done with the element.

Some of the queue flags are copied into the element flags when you retrieve one with `pop()`

* `SQ_FLAG_OVERRUN` - this means one or more `push()` operations on this queue have failed before the `pop()` call, so there has been data loss.

if `SQ_FLAG_NOWAIT` is passed to `sq_init()`, then (almost) all lock calls can fail and the various `sq_*()` functions might return `SQ_ERR_WOULDBLOCK`. This isn't an error so much as an indication that the `sq_*()` call must be retried. Similar to `O_NONBLOCK` for the POSIX `read()` and `write()` functions.

This repo contains the library in `sq.c` and `sq.h`, an implementation of the `pthread_barrier` API (since OSX doesn't have it), and a stupid/simple demo made up of `main.c`, `t.h` and three thread files, `t1.c`, `t2.c` and `t3.c`. You should be able to build  by running `make`.