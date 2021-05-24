/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#define NAN -1
/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
// bool priority_check(struct list_elem *d1, struct list_elem *d2, void *aux);

void key_push(struct semaphore *sema, int key);
void wake_holder(struct thread *lock_holder);

sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */

void
sema_down (struct semaphore *sema) {
	struct thread *curr, *holder;
	curr = thread_current();
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());
	old_level = intr_disable ();
	while (sema->value == 0) {
		if (curr->key != NAN && curr->lock_holder->donate[curr->key] != curr->priority){
			holder = curr->lock_holder;
			if( holder->priority ==	holder -> donate[curr->key])
				holder->priority = curr->priority;
			holder -> donate[curr->key] = curr->priority;

			if(holder -> status == THREAD_BLOCKED)
				wake_holder(holder);
		}

		list_push_back (&sema->waiters, &thread_current ()->elem);
		list_sort(&sema->waiters, priority_comp, NULL);
		thread_block ();				
	}
	
	sema->value--;
	intr_set_level (old_level);
}


/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	ASSERT (sema != NULL);

	enum intr_level old_level = intr_disable ();
	bool success;
	if (sema->value > 0) {
		sema->value--;
		success = true;
	} else {
		success = false;
	}
	
	intr_set_level (old_level);
	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	ASSERT (sema != NULL);

	enum intr_level old_level = intr_disable ();
	
	if (!list_empty (&sema->waiters)){
		list_sort(&sema->waiters, priority_comp, NULL);
		
		struct thread *t = list_entry (list_pop_front (&sema->waiters),struct thread, elem);
		thread_unblock (t);
		sema->value++;
		
		priority_sort();
		if( !intr_context() )
			priority_yield();
	} else {
		sema->value++;
	}
	intr_set_level (old_level);
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

int
donate (struct thread *t, int priority){
	int donor;
	for (donor = 0; donor < 8; donor++)
		if(t->donate[donor] != NAN)
			break;
	
	t->donate[donor - 1] = priority;
	return donor - 1;
}


void
take (struct thread *thread, struct thread *taker, struct semaphore *sema){
	int i, key;
	key = taker->key;
	for(i=0; i<8; i++){
		if(thread->donate[i]!=NAN)
			break;
	}
	if(i==8){
		return;
	}
	if(i!=8){
		if( thread->priority == thread->donate[key] ){
			thread->donate[key] = NAN;
			key = NAN;
			int j;
			for(j =0; j<8; j++){
				if(thread->donate[j] != NAN){
					thread->priority = thread->donate[j];
					break;
				}
			}
			if(j==8)
				thread->priority = thread->own_priority;
			
		}
		else{
			thread->donate[key] = NAN;
			taker->key = NAN;
		}
	}
}
/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));
	struct thread *lock_holder = lock->holder;
	enum intr_level old_level;
	int key = NAN;
	if(	(&lock->semaphore)->value == 0 && lock_holder != NULL){
		if((lock_holder) -> priority < thread_get_priority()){
			key=donate(lock_holder, thread_get_priority());
			lock_holder -> priority = thread_get_priority();
			thread_current()->key = key;
			thread_current()->lock_holder = lock_holder;
			if(lock_holder -> status == THREAD_BLOCKED){
				wake_holder(lock_holder);
			}
		}
	}
	if(key == NAN){
		sema_down (&lock->semaphore);
	}
	else{
		sema_down (&lock->semaphore);
		take (lock_holder, thread_current(), &lock->semaphore);
		priority_sort();
	}
	lock->holder = thread_current ();
}
void
wake_holder(struct thread *lock_holder){
	ASSERT(lock_holder->status == THREAD_BLOCKED);
	(&lock_holder->elem)->prev->next = (&lock_holder->elem)->next;
	(&lock_holder->elem)->next->prev = (&lock_holder->elem)->prev;
	thread_unblock(lock_holder);

}
	
/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));
	
	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}
/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

bool
sema_comp (struct list_elem *e1, struct list_elem *e2, void *aux) {
		struct semaphore sema1 = list_entry (e1, struct semaphore_elem, elem)->semaphore;
		struct semaphore sema2 = list_entry (e2, struct semaphore_elem, elem)->semaphore;
		struct thread *thread1 = list_entry (list_front (&sema1.waiters), struct thread, elem);
		struct thread *thread2 = list_entry (list_front (&sema2.waiters), struct thread, elem);
		return thread1->priority > thread2->priority;
}
/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);	
	list_sort (&cond->waiters, sema_comp, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));
	if (list_empty (&cond->waiters))
		return;
	list_sort (&cond->waiters, sema_comp, NULL);
	sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
}


/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
