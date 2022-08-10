/* This is the only file you should update and submit. */

/* Fill in your Name and GNumber in the following two comment fields
 * Name: Nghia Nguyen
 * GNumber: 01339007
 */

#include <sys/wait.h>
#include "taskman.h"
#include "parse.h"
#include "util.h"

/* Constants */
#define DEBUG 0
#define NUM_PATHS 2

// The following two arrays may help; uncomment if you plan to use them.
static const char *taskman_path[] = { "./", "/usr/bin/", NULL };
static const char *instructions[] = { "quit", "help", "tasks", "delete", "run", "bg", "cancel", "log", "output", "suspend", "resume", NULL};

/* A linked list for Task */
typedef struct task_node {
    char cmd[MAXLINE]; // Command of the Task being run
    char *argv[MAXARGS]; //Arguments

    int status; //State of the Task being run
    int foreground; //Running on foreground or not
    int tid; //Task ID of the current task
    pid_t pid; // PID of the Task you're Tracking
    int exit_code; //Exit code for the Task 
    struct task_node *next; // Pointer to next Task Node in a linked list.
} task_node_t;

/* Linked list Header Definition */
typedef struct task_header {
  int count; // How many tasks are in this linked list?
  task_node_t *head; // Points to FIRST node of linked list.
} task_header_t;

/* Global variables*/
static task_header_t *header;
static char path0[MAXARGS];
static char path1[MAXARGS];
static int sigINT_received = 0;

/* Functions Prototypes */
task_header_t *create_linkedList();
int quit_help_tasks(char *cmd, task_header_t *header);
task_node_t *new_task(char *command, char *argv[MAXARGS]);
void insert_task(task_header_t *header, task_node_t *task);
task_node_t *locate_previous_task(task_node_t *currentTask, task_node_t *task , int tid);
int delete_task(task_header_t *header, int tid);
task_node_t *locate_task(task_node_t *task, int tid);
task_node_t *locate_task_tid(task_node_t *task, int tid);
task_node_t *locate_task_state(task_node_t *task);
int execute_fg_task(task_node_t *task, char *file);
int execute_bg_task(task_node_t *task, char *file);
int execute_log_task(task_node_t *task, char *file);
int execute_output(int tid);
int run_task(task_header_t *header, int tid, char *file, int runMode);
void bg_child_handler(int sig);
void fg_child_handler(int sig);
int status_changed_by_signal(pid_t pid, int status, int exit_code, int type, int transition);
int fg_status_change(pid_t pid, int status, int exit_code, int type, int transition);
task_node_t *locate_task_pid(task_node_t *task, pid_t pid);
int control_task(int tid, int action);
void block_signal(int option, int sig);

/* The entry of your task management program */
int main() {
    char cmdline[MAXLINE];        /* Command line */
    char *cmd = NULL;

    /* Intial Prompt and Welcome */
    log_intro();
    log_help();

    /* Shell looping here to accept user command and execute */
    while (1) {
        char *argv[MAXARGS];        /* Argument list */
        Instruction inst;           /* Instruction structure: check parse.h */

        /* Print prompt */
        log_prompt();

        /* Read a line */
        // note: fgets will keep the ending '\n'
        errno = 0;
        if (fgets(cmdline, MAXLINE, stdin) == NULL) {
            if (errno == EINTR) {
                continue;
            }
            exit(-1);
        }

        if (feof(stdin)) {  /* ctrl-d will exit text processocr */
            exit(0);
        }

        /* Parse command line */
        if (strlen(cmdline)==1)   /* empty cmd line will be ignored */
            continue;     

        cmdline[strlen(cmdline) - 1] = '\0';        /* remove trailing '\n' */

        cmd = malloc(strlen(cmdline) + 1);
        snprintf(cmd, strlen(cmdline) + 1, "%s", cmdline);

        /* Bail if command is only whitespace */
        if(!is_whitespace(cmd)) {
            initialize_command(&inst, argv);    /* initialize arg lists and instruction */
            parse(cmd, &inst, argv);            /* call provided parse() */

            if (DEBUG) {  /* display parse result, redefine DEBUG to turn it off */
                debug_print_parse(cmd, &inst, argv, "main (after parse)");
            }

            /* After parsing: your code to continue from here */
            /*================================================*/

        }


        if(header == NULL) {
            header = create_linkedList();
        }
        //quit help tasks
        if(quit_help_tasks(inst.instruct, header)) {
            continue;
        }
        //Delete
        if(strcmp(inst.instruct, instructions[3]) == 0) {
            if(delete_task(header, inst.id)) {
                log_delete(inst.id);
            }
            else {
                log_task_id_error(inst.id);
            }
            continue;
        }
        //Foreground
        if(strcmp(inst.instruct, instructions[4]) == 0) {
            run_task(header, inst.id, inst.file, LOG_FG);
            continue;
        }
        //Background
        if(strcmp(inst.instruct, instructions[5]) == 0) {
            run_task(header, inst.id, inst.file, LOG_BG);
            continue;
        }
        //0: cancel
        if(strcmp(inst.instruct, instructions[6]) == 0) {
            control_task(inst.id, 0);
            continue;
        }
        //Suspend
        if(strcmp(inst.instruct, instructions[9]) == 0) {
            control_task(inst.id, 1);
            continue;
        }
        //Resume
        if(strcmp(inst.instruct, instructions[10]) == 0) {
            control_task(inst.id, 2);
            continue;
        }
        //Log
        if(strcmp(inst.instruct, instructions[7]) == 0) {
            run_task(header, inst.id, inst.file, LOG_LOG_BG);
            continue;
        }
        //Output
        if(strcmp(inst.instruct, instructions[8]) == 0) {
            task_node_t *currentTask = locate_task_tid(header->head, inst.id);

            //The task is not found
            if(currentTask == NULL) {
                log_task_id_error(inst.id);
                continue;
            }

            else { 
                log_output_begin(inst.id);
                execute_output(inst.id);
            }

            continue;
        }

        //Adding a new task to the linked list
        insert_task(header, new_task(cmd, argv));

        free(cmd);
        cmd = NULL;
        free_command(&inst, argv);
    }
    return 0;
}

/**
 * @brief Run a task based on its running mode
 * 
 * @param header of the linked list
 * @param tid of the task
 * @param file input
 * @param runMode background or foreground
 * @return int 
 */
int run_task(task_header_t *header, int tid, char *file, int runMode) {
    
    //Finding the task with matched tid
    task_node_t *currentTask = locate_task_tid(header->head, tid);

    //The task is not found
    if(currentTask == NULL) {
        log_status_error(tid, 0);
    }
    else {
        //Running as foreground mode
        if(runMode == LOG_FG) {
            currentTask->status = LOG_STATE_WORKING;
            currentTask->foreground = 1;
            execute_fg_task(currentTask, file);
        }

        //Running as foreground mode
        else if(runMode == LOG_BG) {
            //If the current task's status is standby
            if(currentTask->status == 0) {
                currentTask->status = LOG_STATE_WORKING;
                currentTask->foreground = 0;
                execute_bg_task(currentTask, file);
            }
        }
        //Running as log mode
        else if(runMode == LOG_LOG_BG) {
            if(currentTask->status == 0) {
                currentTask->status = LOG_STATE_WORKING;
                currentTask->foreground = 0;
                execute_log_task(currentTask, file);
            }
        }
    }

    return 0;
}

/**
 * @brief Changing status of a task after receiving signals
 * 
 * @param pid of the task
 * @param status of the task
 * @param exit_code of the task
 * @param type of the task
 * @param transition of the task
 * @return int 
 */
int status_changed_by_signal(pid_t pid, int status, int exit_code, int type, int transition) {
    //Finding the task with matched tid
    task_node_t *task = locate_task_pid(header->head, pid);
    //The task is not found
    if(task == NULL) {
        return -1;
    }

    //Printing out status message
    log_status_change(task->tid, task->pid, type, task->cmd, transition);
    
    //blocking signal
    block_signal(0,0);

    //Changing status
    task->status = status;
    task->foreground = 0;
    //Set exit code if suspended
    if(status != LOG_STATE_SUSPENDED) {
        task->exit_code = exit_code;
    }

    //unblocking signal
    block_signal(1,0);
    
    return 1;
}

/**
 * @brief Function to cancel, suspend, or resume a task
 * 
 * @param tid of the task
 * @param action to perform
 * @return int 
 */
int control_task(int tid, int action) {
    //Finding the task with matched tid
    task_node_t *currentTask = locate_task_tid(header->head, tid);

    //The task is not found
    if(currentTask == NULL) {
        log_task_id_error(tid);
        return 0;
    }

    //Checking the task's status
    if(currentTask->status == LOG_STATE_WORKING || currentTask->status == LOG_STATE_SUSPENDED) {
        //Canceling
        if(action == 0) {
            log_sig_sent(LOG_CMD_CANCEL, tid, currentTask->pid);
            kill(currentTask->pid, SIGINT);
        }
        //Suspending
        else if(action == 1) {
            log_sig_sent(LOG_CMD_SUSPEND, tid, currentTask->pid);
            kill(currentTask->pid, SIGTSTP);
        }
        //Resuming
        else if(action == 2) {
            log_sig_sent(LOG_CMD_RESUME, tid, currentTask->pid);
            kill(currentTask->pid, SIGCONT);
        }
    }

    else {
        //Printing out error message
        log_status_error(tid, currentTask->status);
        return 0;
    }

    return 1;
}

/**
 * @brief Functions to execute a "log" task
 * 
 * @param task of the task
 * @param file input
 * @return int 
 */
int execute_log_task(task_node_t *task, char *file) {
    
    //Process ID for the 1st and 2nd child
    pid_t childPid, childPid1;
    
    //Installing signal handler
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    struct sigaction old;
    act.sa_handler = bg_child_handler;
    sigaction(SIGCHLD, &act, &old);

    //Coppying paths
    strcpy(path0, string_copy(taskman_path[0]));
    strcpy(path1, string_copy(taskman_path[1]));

    //Creating a pipe
    //Array for pipe's file descriptors for buffering data.
    int pipe_fds[2] = {0};
    int succ = pipe(pipe_fds);

    //Fail to create a pipe
    if(succ == -1) {
        printf("Couldn't create pipe \n");
        return 0;
    }

    //Setting file descriptors to pipe's output and input
    int in_p = pipe_fds[1];
    int out_p = pipe_fds[0];

    int execStatus;
    //blocking signal
    block_signal(0,0);

    //Forking the 1st child
    childPid = fork();
    setpgid(0,0);

    //Parent runs this
    if(childPid != 0 && childPid != -1) {
        //unblocking signal
        block_signal(1,0);

        //Saving 1st child's PID
        task->pid = childPid;
        log_status_change(task->tid, childPid, LOG_BG, task->cmd, LOG_START);

        //blocking signal
        block_signal(0,0);

        //Forking the 2nd child
        childPid1 = fork();

        //2nd child runs this
        if(childPid1 == 0) {
            setpgid(0,0);

            //unblocking signal
            block_signal(1,0);

            //Closing the pipe's input side
            close(in_p);
            //Redirecting the current process's input to the pipe's output side
            dup2(out_p, STDIN_FILENO);

            //Creating the log file name associated with task's ID
            char logName[MAXARGS];
            char num[MAXARGS];
            sprintf(num, "%d", task->tid);
            strcpy(logName, "log");
            strcat(logName, num);
            strcat(logName, ".txt");

            //Executing the tee command
            execStatus = execl("./tee", "tee", logName, NULL); 
            if(execStatus == -1) {
                execStatus = execl("/usr/bin/tee", "tee", logName, NULL); 
                //Fail to run exec command
                if(execStatus == -1) {
                    log_run_error(task->cmd);
                    task->status = LOG_STATE_COMPLETE;
                    task->exit_code = 1;

                    childPid = -1;
                    //Terminate the child
                    kill(getpid(), 9);
                
                    return -1;
                }
            }
        }

        //unblocking signal
        block_signal(1,0);

        //Parent closes both ends of the pipe
        close(in_p);
        close(out_p);
    }
    
    //1st Child runs this
    if(childPid == 0) {
        setpgid(0,0);

        //Checking for file input
        if(file != NULL) {
            int in = open(file, O_RDONLY);
            if(in == -1) {
                //Fail to open input file
                log_file_error(task->tid, file);
                log_run_error(task->cmd);

                task->status = LOG_STATE_COMPLETE;
                task->exit_code = 1;

                childPid = -1;
                kill(getpid(), 9);
                
                return -1;
            }
            dup2(in, STDIN_FILENO);
            close(in);
        }
        //Coppying paths
        strcat(path0, task->argv[0]);
        strcat(path1, task->argv[0]); 

        //unblocking signal
        block_signal(1,0);

        //Closing the the pipe's output side
        close(out_p);
        //Redirecting the current process's output to the pipe's input side
        dup2(in_p, STDOUT_FILENO);

        //Executing the task command
        execStatus = execv(path0, task->argv); 
        if(execStatus == -1) {
            execStatus = execv(path1, task->argv); 
            //Fail to run exec command
            if(execStatus == -1) {
                log_run_error(task->cmd);

                task->status = LOG_STATE_COMPLETE;
                task->exit_code = 1;

                childPid = -1;
                //Terminate the child
                kill(getpid(), 9);
                
                return -1;
            }
        }
    }

    return childPid;
}

/**
 * @brief Signal handler to handle background task
 * 
 * @param sig Signal received
 */
void bg_child_handler(int sig) {
    //Status and PID
    int status = 0;
    pid_t child_pid = 0;

    do {
        //Reaping a child process
        child_pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if(child_pid <= 0) {
            //No more zombies
            return; 
        }
        //If the child exits normally
        if(WIFEXITED(status)) {
            status_changed_by_signal(child_pid, LOG_STATE_COMPLETE, 0, LOG_BG, LOG_CANCEL);
        }
        //If the child is terminated by signal
        else if(WIFSIGNALED(status)) {
            status_changed_by_signal(child_pid, LOG_STATE_KILLED, 0, LOG_BG, LOG_CANCEL_SIG);
        }
        //If the child is stopped by signal
        else if(WIFSTOPPED(status)) {
            status_changed_by_signal(child_pid, LOG_STATE_SUSPENDED, 0, LOG_BG, LOG_SUSPEND);
        }
        //If the child is resumed by signal
        else if(WIFCONTINUED(status)) {
            status_changed_by_signal(child_pid, LOG_STATE_WORKING, 0, LOG_BG, LOG_RESUME);
        }

        else {
            status_changed_by_signal(child_pid, LOG_STATE_COMPLETE, 1, LOG_BG, LOG_CANCEL);
        }
    } while(child_pid > 0);

    return;
}

/**
 * @brief Functions to run a task in background
 * 
 * @param task to be ran
 * @param file input
 * @return int 
 */
int execute_bg_task(task_node_t *task, char *file) {
    
    pid_t childPid;
    
    //Installing signal handler
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    struct sigaction old;
    act.sa_handler = bg_child_handler;
    sigaction(SIGCHLD, &act, &old);
    //Coppying paths
    strcpy(path0, string_copy(taskman_path[0]));
    strcpy(path1, string_copy(taskman_path[1]));

    int execStatus;
    //blocking signal
    block_signal(0,0);
    //Forking a child
    childPid = fork();
    setpgid(0,0);

    //Parent runs this
    if(childPid != 0 && childPid != -1) {
        task->pid = childPid;

        //unblocking signal
        block_signal(1,0);
        log_status_change(task->tid, childPid, LOG_BG, task->cmd, LOG_START);
    }
    
    //Child runs this
    if(childPid == 0) {
        setpgid(0,0);
        //Checking for file input
        if(file != NULL) {
            int in = open(file, O_RDONLY);
            if(in == -1) {
                //Fail to open input file
                log_file_error(task->tid, file);
                log_run_error(task->cmd);

                task->status = LOG_STATE_COMPLETE;
                //task->pid = getpid();
                task->exit_code = 1;

                childPid = -1;
                kill(getpid(), 9);
                
                return -1;
            }
            dup2(in, STDIN_FILENO);
            close(in);
        }
        //Coppying paths
        strcat(path0, task->argv[0]);
        strcat(path1, task->argv[0]); 

        //unblocking signal
        block_signal(1,0);

        execStatus = execv(path0, task->argv); 
        if(execStatus == -1) {
            execStatus = execv(path1, task->argv); 
            //Fail to run exec command
            if(execStatus == -1) {
                log_run_error(task->cmd);

                task->status = LOG_STATE_COMPLETE;
                task->exit_code = 1;

                childPid = -1;
                //Terminate the child
                kill(getpid(), 9);
                
                return -1;
            }
        }
    }
    
    return childPid;
}

/**
 * @brief Signal handler to handle foreground task
 * 
 * @param sig Signal received
 */
void fg_child_handler(int sig) {
    //Status and PID
    int status = 0;
    pid_t child_pid = 0;
    //If the child exits normally
    if(sig == SIGCHLD) {    
        //Reaping a child process
        child_pid = waitpid(-1, &status, WUNTRACED);

        if(WIFEXITED(status)) {
            status_changed_by_signal(child_pid, LOG_STATE_COMPLETE, 0, LOG_FG, LOG_CANCEL);
        }

        else {
            if(sigINT_received == 0) {
                status_changed_by_signal(child_pid, LOG_STATE_COMPLETE, 1, LOG_FG, LOG_CANCEL);
            }
            sigINT_received = 0;
        }
    }
    //If the child is terminated by signal
    else if(sig == SIGINT) {
        fg_status_change(0, LOG_STATE_KILLED, 0, LOG_FG, LOG_CANCEL_SIG);
        sigINT_received = 1;
    }
    //If the child is stopped by signal
    else if(sig == SIGTSTP) {
        fg_status_change(0, LOG_STATE_SUSPENDED, 0, LOG_FG, LOG_SUSPEND);
        sigINT_received = 1;
    }

    return;
}

/**
 * @brief Functions to run a task in foreground
 * 
 * @param task to be ran
 * @param file input
 * @return int 
 */
int execute_fg_task(task_node_t *task, char *file) {
    //Process ID of a child
    pid_t childPid;
    int execStatus;

    //Installing signal handler
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    struct sigaction old;
    act.sa_handler = fg_child_handler;
    sigaction(SIGCHLD, &act, &old);
    sigaction(SIGINT, &act, &old);
    sigaction(SIGTSTP, &act, &old);

    //Coppying paths
    strcpy(path0, string_copy(taskman_path[0]));
    strcpy(path1, string_copy(taskman_path[1]));
    //Forking a child
    childPid = fork();

    //Parent runs this
    if(childPid != 0 && childPid != -1) {
        log_status_change(task->tid, childPid, LOG_FG, task->cmd, LOG_START);
        task->pid = childPid;
    }

    //Child run this
    if(childPid == 0) {
        setpgid(0,0);
        //Checking for file input
        if(file != NULL) {
            int in = open(file, O_RDONLY);
            if(in == -1) {
                //Fail to open input file
                log_file_error(task->tid, file);
                log_run_error(task->cmd);

                kill(getpid(), 9);

                return -1;
            }
            dup2(in, STDIN_FILENO);
            close(in);
        }
        //Coppying paths
        strcat(path0, task->argv[0]);
        strcat(path1, task->argv[0]);
        //Executing the task command
        execStatus = execv(path0, task->argv); 
        if(execStatus == -1) {
            execStatus = execv(path1, task->argv); 
            //Fail to run exec command
            if(execStatus == -1) {
                log_run_error(task->cmd);
                //Terminate the child
                kill(getpid(), 9);
                
                return -1;
            }
        }
    }
    
    return childPid;
}
/**
 * @brief Function to change status of foreground tasks by signal
 * 
 * @param pid of the task
 * @param status of the task
 * @param exit_code of the task
 * @param type of the task
 * @param transition of the task
 * @return int 
 */
int fg_status_change(pid_t pid, int status, int exit_code, int type, int transition) {
    //Finding the task with matched state
    task_node_t *task = locate_task_state(header->head);
     //The task is not found
    if(task == NULL) {
        return 0;
    }
    //If ctrl-c was received
    if(status == LOG_STATE_KILLED) {
        //Canceling
        kill(task->pid, SIGINT);
        task->exit_code = 0;
        log_ctrl_c();
        status_changed_by_signal(task->pid, LOG_STATE_KILLED, 0, LOG_FG, LOG_CANCEL_SIG);
    }
    //If ctrl-z was received
    else if(status == LOG_STATE_SUSPENDED) {
        //Suspending
        kill(task->pid, SIGTSTP);
        log_ctrl_z();
        status_changed_by_signal(task->pid, LOG_STATE_SUSPENDED, 0, LOG_FG, LOG_SUSPEND);
    }
    
    return 1;
}
/**
 * @brief Functions to block signal
 * 
 * @param option 
 * @param sig 
 */
void block_signal(int option, int sig) {
    sigset_t mask, prev_mask;
    sigaddset(&mask, SIGCHLD);
    //Blocking
    if(option == 0) {
        sigprocmask(SIG_BLOCK, &mask, &prev_mask);
    }
    //Unblocking
    else if(option == 1) {
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    }
}

/**
 * @brief Create a linkedList object
 * 
 * @return task_header_t* header of a linkedList
 */
task_header_t *create_linkedList() {
    struct task_header *header = (task_header_t*) malloc(sizeof(task_header_t));
    header->head = NULL;
    header->count = 0;
    return header;
}

/**
 * @brief Insert a task node to a list, which is a singly linked list
 *  
 * @param header the head of a list
 * @param task to be inserted
 */
void insert_task(task_header_t *header, task_node_t *task) {
    //blocking signal
    block_signal(0, 0);
    
    //Checking if the list is empty
    if(header->head == NULL) {
        task->tid = 1;
        header->head = task;
        header->count = 1;
        log_task_init(task->tid, task->cmd);
        //unblocking signal
        block_signal(1,0);

        return;
    }

    else {
        //Checking if the pid of first task node in the linked list is greater than 1
        if(header->head->tid > 1) {
            //Adding the task node as the first element in the defunct queue
            task->tid = 1;
            task->next = header->head;
            header->head = task;
            header->count++;
        }

        else {
            //Looping through the defunct queue to find the previous task node of task node
            task_node_t *previousTask = locate_previous_task(header->head, task, task->tid);
            //Adding the task node after the previous task node
            task->next = previousTask->next;
            previousTask->next = task;
            header->count++;
        }
    }

    //unblocking signal
    block_signal(1,0);

    log_task_init(task->tid, task->cmd);
    return;
}

/* Remove the process with matching pid from Ready or Suspended Queues and add the Exit Code to it.
 * Follow the specification for this function.
 * Returns its exit code (from flags) on success or a -1 on any error.
 */
int delete_task(task_header_t *header, int tid) {
    //blocking signal
    block_signal(0, 0);

    //Checking if the linked list are emtpy
  if(header->head == NULL) {
    //unblocking signal
    block_signal(1,0);
    return 0;
  }

  //Search the linked list for matched tid task node
  //Setting the current task node refer to the linked list's head
  task_node_t *currentTask = header->head;

  //Checking if the first element of ready queue has a matched tid
  if(currentTask->tid == tid) {
    //Removing the matched tid task node from the ready queue
    header->head = currentTask->next;
    header->count--;
    currentTask->next = NULL;

    //unblocking signal
    block_signal(1,0);

    return 1;
  }

  else {
    //Calling locate_task functions to find previous task node of matched tid task node
    task_node_t *previousTask = locate_task(currentTask, tid);
    
    if(previousTask->next != NULL) {
      //Saving the matched tid task address to currentTask pointer
      currentTask = previousTask->next;
      //Removing the matched tid Task node from the linked list
      previousTask->next = currentTask->next;
      currentTask->next = NULL;
      
      //Decreasing number of element in linked list
      header->count--;
      
      //unblocking signal
      block_signal(1,0);

      return 1;
    }
  }
    //unblocking signal
    block_signal(1,0);

  return 0;
}
/**
 * @brief Function to run "output" command
 * 
 * @param tid of the task
 * @return int 
 */
int execute_output(int tid) {
    //PID of child process
    pid_t childPid;
    int execStatus = 0;
    int fd = 0;
    //Coppying paths
    strcpy(path0, string_copy(taskman_path[0]));
    strcpy(path1, string_copy(taskman_path[1]));
    //Forking a child
    childPid = fork();

    //Child run this
    if(childPid == 0) {
        setpgid(0,0);

        //Creating the log file name associated with task's ID
        char logName[MAXARGS];
        char num[MAXARGS];
        sprintf(num, "%d", tid);
        strcpy(logName, "log");
        strcat(logName, num);
        strcat(logName, ".txt");
        strcat(path0, logName);
        strcat(path1, logName);
        //Openning a file associated with task's ID
        fd = open(path0, O_RDONLY);
        if(fd == -1) {
            fd = open(path1, O_RDONLY);
            if(fd == -1) {
                //Fail to open file
                log_output_unlogged(tid);
                return -1;
            }
        }
        
        close(fd);
        //Executing the "cat" command
        execStatus = execl("./cat", "cat", logName, NULL); 
        if(execStatus == -1) {
            execStatus = execl("/usr/bin/cat", "cat", logName, NULL);
            //Fail to run exec command 
            if(execStatus == -1) {
                //Terminate the child
                kill(getpid(), 9);
                return -1;
            }
        }
    }

    return childPid;
}

/**
 * @brief Recursive function to go through a queue to find the previous task node
 * @param task which is the head of a queue
 * @param tid of the task, which has greater tid
 * @return task_node_t* refers to the of previous node of matched tid task node
 */
task_node_t *locate_previous_task(task_node_t *currentTask, task_node_t *task , int tid) {
    //Base case: Reaching the end of the queue or found the matched pid task node
    if(currentTask->next == NULL || currentTask->next->tid > tid) {
        task->tid = tid;
        return currentTask;
    }
    //Recursive case
    else {
        return currentTask = locate_previous_task(currentTask->next, task, ++tid);
    }
}

/**
 * @brief Recursive function to go through a queue to find the previous task node
 * @param task which is the head of a queue
 * @param tid of the task node needs to be found
 * @return task_node_t* refers to the of previous node of matched tid task node
 */
task_node_t *locate_task(task_node_t *task, int tid) {
    //Base case: Reaching the end of the queue or found the matched tid task node
    if(task->next == NULL || task->next->tid == tid) {
        return task;
    }
    //Recursive case
    else {
        return task = locate_task(task->next, tid);
    }
}

/**
 * @brief Recursive function to go through a queue to find the task node
 * @param task which is the head of a queue
 * @param tid of the task node needs to be found
 * @return task_node_t* refers to the of previous node of matched tid task node
 */
task_node_t *locate_task_tid(task_node_t *task, int tid) {
    //Base case: Reaching the end of the queue or found the matched tid task node
    if(task == NULL || task->tid == tid) {
        return task;
    }
    //Recursive case
    else {
        return task = locate_task_tid(task->next, tid);
    }
}

/**
 * @brief Recursive function to go through a queue to find the matched pid task node 
 * @param task which is the head of a queue
 * @param pid of the task node needs to be found
 * @return task_node_t* refers to the of previous node of matched pid task node
 */
task_node_t *locate_task_pid(task_node_t *task, pid_t pid) {
    //Base case: Reaching the end of the queue or found the matched tid task node
    if(task == NULL || task->pid == pid) {
        return task;
    }
    //Recursive case
    else {
        return task = locate_task_pid(task->next, pid);
    }
}

/**
 * @brief Recursive function to go through a queue to find the matched state task node 
 * @param task which is the head of a queue
 * @param state of the task node needs to be found
 * @return task_node_t* refers to the of previous node of matched pid task node
 */
task_node_t *locate_task_state(task_node_t *task) {
    task = header->head;
    //Base case: Reaching the end of the queue or found the matched tid task node
    if(task == NULL || (task->status == LOG_STATE_WORKING && task->foreground == 1)) {
        return task;
    }
    //Recursive case
    else {
        return task = locate_task_state(task->next);
    }
}

/* Create a new task_node_t with the given information.
 * Returns the task_node_t on success or a NULL on any error.
 */
task_node_t *new_task(char *command, char *argv[MAXARGS]) {
    //Dynamically allocate a new process node
    task_node_t *newTask = (task_node_t*) malloc(sizeof(task_node_t));
    //Checking for error
    if(newTask == NULL) {
        return NULL;
    }

    //Copying the command to cmd
    strcpy(newTask->cmd, string_copy(command));

    for(int i = 0; i < MAXARGS && argv[i] != NULL; i++) {
        newTask->argv[i] = string_copy(argv[i]);
    }

    //Setting pid, state
    newTask->tid = 2;
    newTask->pid = 0;
    newTask->exit_code = 0;
    newTask->status = 0;
    newTask->foreground = 0;
    newTask->next = NULL;

    return newTask;
}
/**
 * @brief Function to print out content of a linkedlist
 * 
 * @param header the head of the linked list
 */
void linkedListToString(task_header_t *header) {
    //Checking if the linkedlist is empty
    if(header->head == NULL) {
        log_num_tasks(header->count);
        return;
    }

    else {
        //Printing out number of tasks
        log_num_tasks(header->count);
        task_node_t *task = header->head;
        //Looping through the linkedlist to print out content of each node
        while(task != NULL) {
            log_task_info(task->tid, task->status, task->exit_code, task->pid, task->cmd);
            task = task->next;
        }
    }

    return;
}

/**
 * @brief Function to run quit, help, tasks command
 * 
 * @param cmd type of command
 */
int quit_help_tasks(char *cmd, task_header_t *header) {
    //Calling log_quit() and quit the taskman
    if(!strcmp(cmd, instructions[0])) {
        log_quit();
        exit(0);
    }

    //Calling log_help()
    else if(!strcmp(cmd, instructions[1])) {
        log_help();
        return 1;
    }
    //Tasks command for content of all tasks
    else if(!strcmp(cmd, instructions[2])) {
        linkedListToString(header);
        return 1;
    }

    return 0;
}
