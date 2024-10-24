#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "scheduler.h"

#include <assert.h>
#include <curses.h>
#include <ucontext.h>

#include "util.h"

// This is an upper limit on the number of tasks we can create.
#define MAX_TASKS 128

// This is the size of each task's stack memory
#define STACK_SIZE 65536

typedef enum task_state {
  running, waiting, sleeping, exited, waitingForInput}
  task_state_t;

// This struct will hold the all the necessary information for each task
typedef struct task_info {
  // This field stores all the state required to switch back to this task
  ucontext_t context;

  // This field stores another context. This one is only used when the task
  // is exiting.
  ucontext_t exit_context;

  // TODO: Add fields here so you can:
  //   a. Keep track of this task's state.
  task_state_t taskState;
  //   b. If the task is sleeping, when should it wake up?
  size_t wakeTime;
  //   c. If the task is waiting for another task, which task is it waiting for?
  task_t waitingFor;
  //   d. Was the task blocked waiting for user input? Once you successfully
  //      read input, you will need to save it here so it can be returned.
  int userInput;
  // bool waitingForInput;
} task_info_t;

int current_task = 0;          //< The handle of the currently-executing task
int num_tasks = 1;             //< The number of tasks created so far
task_info_t tasks[MAX_TASKS];  //< Information for every task

/**
 * Initialize the scheduler. Programs should call this before calling any other
 * functiosn in this file.
 */
void scheduler_init() {
  // TODO: Initialize the state of the scheduler
  tasks[0].taskState = running; //main program
  getcontext(&tasks[0].context);
  tasks[0].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[0].context.uc_stack.ss_size = STACK_SIZE;
}

int find_task(bool exit) {
  int next_task = (current_task + 1) % num_tasks;
  if (exit) {
    while (tasks[next_task].taskState == exited || (tasks[next_task].taskState == sleeping && tasks[next_task].wakeTime > time_ms()) || (tasks[next_task].taskState == waiting && tasks[tasks[next_task].waitingFor].taskState != exited)) {
      next_task = (next_task + 1) % num_tasks;
    }
    return next_task;
  } else {
    int ch;
    while (tasks[next_task].taskState == exited || (tasks[next_task].taskState == sleeping && tasks[next_task].wakeTime > time_ms()) || (tasks[next_task].taskState == waiting && tasks[tasks[next_task].waitingFor].taskState != exited) || (next_task == 0) || ((tasks[next_task].taskState == waitingForInput) && ((ch = getch()) == ERR))) {
      next_task = (next_task + 1) % num_tasks;
    }
    if (ch != ERR) {
      tasks[next_task].userInput = ch;
    }
    return next_task;
  }
}


/**
 * This function will execute when a task's function returns. This allows you
 * to update scheduler states and start another task. This function is run
 * because of how the contexts are set up in the task_create function.
 */
void task_exit() {
  tasks[current_task].taskState = exited;
  int next_task = find_task(true);
  if (tasks[next_task].taskState == exited) {
    exit(0);
  }
  int temp = current_task;
  current_task = next_task;
  swapcontext(&tasks[temp].context, &tasks[next_task].context);
}

/**
 * Create a new task and add it to the scheduler.
 *
 * \param handle  The handle for this task will be written to this location.
 * \param fn      The new task will run this function.
 */
void task_create(task_t* handle, task_fn_t fn) {
  // Claim an index for the new task
  int index = num_tasks;
  num_tasks++;

  // Set the task handle to this index, since task_t is just an int
  *handle = index;

  // We're going to make two contexts: one to run the task, and one that runs at the end of the task
  // so we can clean up. Start with the second

  // First, duplicate the current context as a starting point
  getcontext(&tasks[index].exit_context);

  // Set up a stack for the exit context
  tasks[index].exit_context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].exit_context.uc_stack.ss_size = STACK_SIZE;

  // Set up a context to run when the task function returns. This should call task_exit.
  makecontext(&tasks[index].exit_context, task_exit, 0);

  // Now we start with the task's actual running context
  getcontext(&tasks[index].context);

  // Allocate a stack for the new task and add it to the context
  tasks[index].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].context.uc_stack.ss_size = STACK_SIZE;

  // Now set the uc_link field, which sets things up so our task will go to the exit context when
  // the task function finishes
  tasks[index].context.uc_link = &tasks[index].exit_context;

  // And finally, set up the context to execute the task function
  makecontext(&tasks[index].context, fn, 0);
}

/**
 * Wait for a task to finish. If the task has not yet finished, the scheduler should
 * suspend this task and wake it up later when the task specified by handle has exited.
 *
 * \param handle  This is the handle produced by task_create
 */
void task_wait(task_t handle) {
  //Block this task until handle has exited.
  if (tasks[handle].taskState == exited) {
    return;
  }
  //Set the current task's task state to waiting
  tasks[current_task].taskState = waiting; // current_task is waiting
  tasks[current_task].waitingFor = handle; // the current_task is waiting for handle to finish
  // tasks[current_task].waitingForInput = false;
  int temp = current_task;
  current_task = handle;
  tasks[current_task].taskState = running;
  swapcontext(&tasks[temp].context, &tasks[current_task].context); // switch to the next task

}

/**
 * The currently-executing task should sleep for a specified time. If that time is larger
 * than zero, the scheduler should suspend this task and run a different task until at least
 * ms milliseconds have elapsed.
 *
 * \param ms  The number of milliseconds the task should sleep.
 */
void task_sleep(size_t ms) {
  // TODO: Block this task until the requested time has elapsed.
  // Hint: Record the time the task should wake up instead of the time left for it to sleep. The
  // bookkeeping is easier this way.
  tasks[current_task].taskState = sleeping; // set the current_task's state to sleeping
  tasks[current_task].wakeTime = ms + time_ms(); // calculate the time the task should wake up
  int next_task = find_task(false);
  // Set the state to running
  tasks[next_task].taskState = running;
  int temp = current_task;
  current_task = next_task; // switch to the new task
  swapcontext(&tasks[temp].context, &tasks[next_task].context); // switch to the next task
}

/**
 * Read a character from user input. If no input is available, the task should
 * block until input becomes available. The scheduler should run a different
 * task while this task is blocked.
 *
 * \returns The read character code
 */
int task_readchar() {
  // TODO: Block this task until there is input available.
  // To check for input, call getch(). If it returns ERR, no input was available.
  // Otherwise, getch() will returns the character code that was read.
  // Once input becomes available, save it in tasks[current_task].userInput and
  // return it.
  tasks[current_task].taskState = waitingForInput;
  int next_task = find_task(false);
  int temp = current_task;
  current_task = next_task;
  swapcontext(&tasks[temp].context, &tasks[next_task].context); // switch to the next task
  return tasks[current_task].userInput;
}