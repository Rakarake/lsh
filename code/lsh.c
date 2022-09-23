/* 
 * Main source code file for lsh shell program
 *
 * You are free to add functions to this file.
 * If you want to add functions in a separate file 
 * you will need to modify Makefile to compile
 * your additional functions.
 *
 * Add appropriate comments in your code to make it
 * easier for us while grading your assignment.
 *
 * Submit the entire lab1 folder as a tar archive (.tgz).
 * Command to create submission archive: 
      $> tar cvf lab1.tgz lab1/
 *
 * All the best 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "parse.h"

#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>

#define TRUE 1
#define FALSE 0

#define READ_END 0
#define WRITE_END 1

void RunCommand(int, Command *);
void DebugPrintCommand(int, Command *);
void PrintPgm(Pgm *);
void stripwhite(char *);

void printenv();
void handle_process(Pgm *pgm, Command *cmd);
void catch_sigint(int signum);
void catch_sigchld(int signum);

extern char **environ;

Command *current_cmd;


int main(void)
{
  Command cmd;
  int parse_result;

  // Register signals
  signal(SIGINT, catch_sigint);
  signal(SIGCHLD, catch_sigchld);

  while (TRUE)
  {
    printf("\n");
    char *line;
    line = readline("> ");

    /* If EOF encountered, exit shell */
    if (!line)
    {
      break;
    }
    /* Remove leading and trailing whitespace from the line */
    stripwhite(line);
    /* If stripped line not blank */
    if (*line)
    {
      add_history(line);
      parse_result = parse(line, &cmd);
      RunCommand(parse_result, &cmd);
    }

    /* Clear memory */
    free(line);
  }
  return 0;
}


void RunCommand(int parse_result, Command *cmd) {
  current_cmd = cmd;

  // Shell commands (exit, cd)
  char *pname = cmd->pgm->pgmlist[0];
  char **pargs = cmd->pgm->pgmlist + 1;
  if ((!strcmp(pname, "exit")) || (!strcmp(pname, ":q"))) {
    exit(0);
  } else if (!strcmp(pname, "cd")) {
    int err;
    if (pargs[0] == NULL) {
      err = chdir(getenv("HOME"));
    } else {
      err = chdir(pargs[0]);
    }
    if (err == -1) {
      fprintf(stderr, "cd: no such file or directory: %s", pargs[0]);
    }
    return;
  }

  // If there is only one argument in the linked list
  if (cmd->pgm->next == NULL) {
    int pid = fork();
    if (pid == -1) {
      fprintf(stderr, "fork failed!\n");
      exit(1);
    }
    if (pid == 0) {
      handle_process(cmd->pgm, cmd);
    } else {
      // Add pid to linked list
      cmd->pgm->pid = pid;
    }
  } else {
    Pgm *p1 = cmd->pgm;
    Pgm *p2;
    while (p1->next != NULL) {
      int pipe_fd[2];
      if (pipe(pipe_fd) == -1) {
        fprintf(stderr, "pipe failed!\n");
        exit(1);
      }

      p2 = p1->next;
      int pid1 = fork();
      if (pid1 == -1) {
        fprintf(stderr, "pipe-fork failed!\n");
        exit(1);
      }
      if (pid1 == 0) {
        // Child1 (reading end)
        close(pipe_fd[WRITE_END]);
        dup2(pipe_fd[READ_END], 0);
        handle_process(p1, cmd);
      } else {
        // Add child1 pid to linked list
        p1->pid = pid1;

        int pid2 = fork();
        if (pid2 == -1) {
          fprintf(stderr, "pipe-fork failed!\n");
          exit(1);
        }
        if (pid2 == 0) {
          // Child2 (writing end)
          close(pipe_fd[READ_END]);
          dup2(pipe_fd[WRITE_END], 1);
          handle_process(p2, cmd);
        } else {
          // Add child2 pid to linked list
          p2->pid = pid2;
        }
      }
      
      p1 = p2;
    }
  }

  // Shell: pause until all pid:s (foreground processes) are resolved
  while (1) {
    int every_pid_resolved = 1;
    Pgm *p = cmd->pgm;
    while (p != NULL) {
      if (p->pid != 0) {
        every_pid_resolved = 0;
        break;
      }
      p = p->next;
    }
    if (!every_pid_resolved) {
      pause();
    } else {
      break;
    }
  }
}

void handle_process(Pgm *pgm, Command *cmd) {
  // Background process
  // Sets the group id to itself; since it does not have the same as the shell,
  // it does not recieve sigals such as SIGINT anymore
  if (cmd->background) {
    setpgid(getpid(), getpid());
  }

  // Output redirection
  if (pgm == cmd->pgm && cmd->rstdout != NULL) {
    // Create new file and connect stdout
    int fd = open(cmd->rstdout, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
      fprintf(stderr, "could not open file: %s\n", cmd->rstdout);
    } else {
      dup2(fd, 1);
    }
  }

  // Input redirection
  if (pgm->next == NULL && cmd->rstdin) {
    // Create new file and connect stdin
    int fd = open(cmd->rstdin, O_RDONLY, O_RDWR);
    if (fd == -1) {
      fprintf(stderr, "could not read file: %s\n", cmd->rstdin);
    } else {
      dup2(fd, 0);
    }
  }

  // Execute program or terminate
  execvp(pgm->pgmlist[0], pgm->pgmlist);
  fprintf(stderr, "lsh: command not found: %s\n", pgm->pgmlist[0]);
  exit(1);
}


// INT (Ctrl-C)
void catch_sigint(int signum) {
  // Set function to handle signal again
  signal(SIGINT, catch_sigint);
}


// CHILD (when child terminates and other things)
void catch_sigchld(int signum) {
  // Clean up terminated process by waiting
  int child_pid = waitpid(-1, NULL, WNOHANG);

  // Go through the linked list and see if it is a foreground process
  int is_fg_process = 0;
  Pgm *p = current_cmd->pgm;
  while (p != NULL) {
    if (child_pid == p->pid) {
      is_fg_process = 1;
      // Resolve this pid
      p->pid = 0;
      break;
    }
    p = p->next;
  }

  if (is_fg_process) {
  } else {
    fprintf(stderr, "background process terminated: %i\n", child_pid);
  }

  //// Wait will fail if foreground process, since we have already waited
  //int child_pid = waitpid(-1, NULL, WNOHANG);
  //// Return of wait is equal to current_pid
  //if (child_pid == fgpid) {
  //  fgpid = 0;
  //  // Foreground process
  //  // Do not create prompt, will happen when calling the read function
  //} else {
  //  // Background process
  //  fprintf(stderr, "background process terminated: %i\n", child_pid);
  //  // If there is a prompt: write it out again
  //  if (fgpid == 0) {
  //    printf("> ");
  //    fflush(stdout);
  //  }
  //}
}


//// Execute the given command(s).
//void RunCommand(int parse_result, Command *cmd) {
//  //DebugPrintCommand(parse_result, cmd);
//
//  // Shell commands (exit, cd)
//  char *pname = cmd->pgm->pgmlist[0];
//  char **pargs = cmd->pgm->pgmlist + 1;
//  if ((!strcmp(pname, "exit")) || (!strcmp(pname, ":q"))) {
//    exit(0);
//  } else if (!strcmp(pname, "cd")) {
//    int err;
//    if (pargs[0] == NULL) {
//      err = chdir(getenv("HOME"));
//    } else {
//      err = chdir(pargs[0]);
//    }
//    if (err == -1) {
//      fprintf(stderr, "cd: no such file or directory: %s", pargs[0]);
//    }
//    return;
//  }
//
//  int pid = fork();
//  if (pid == 0) {
//    // Child process (fork)
//    // Handle input/output file redirection
//    if (cmd->rstdout != NULL) {
//      // Create new file and connect stdout
//      int fd = open(cmd->rstdout, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
//      if (fd == -1) {
//        printf("could not open file: %s\n", cmd->rstdout);
//      } else {
//        dup2(fd, 1);
//      }
//    }
//    if (cmd->rstdin != NULL) {
//      // Create new file and connect stdin
//      int fd = open(cmd->rstdin, O_RDONLY, O_RDWR);
//      if (fd == -1) {
//        printf("could not read file: %s\n", cmd->rstdin);
//      } else {
//        dup2(fd, 0);
//      }
//    }
//    process_pgm(cmd->pgm);
//  } else {
//    // Parent process (shell)
//    if (cmd->background) {
//      //printf("you are free my process!");
//      // Set group pid of background process to something elses so it does not
//      // react to SIGINT
//      setpgid(pid, pid);
//    } else {
//      fgpid = pid;
//      // Wait until fgpid has been resolved by singal hanlder
//      while (fgpid != 0) {
//        pause();
//      }
//      fgpid = 0;
//    }
//  }
//}
//
//// Recursive function to hanlde processes
//void process_pgm(Pgm *pgm) {
//  char *pname =       pgm->pgmlist[0];
//  char **pname_args = pgm->pgmlist;
//
//  if (pgm->next != NULL) {
//    // Create pipe, then fork
//    int pipe_fd[2];
//
//    if (pipe(pipe_fd) == -1) {
//      fprintf(stderr, "pipe failed!\n");
//      exit(1);
//    }
//
//    int ppid = fork();
//    if (ppid == -1) {
//      fprintf(stderr, "pipe-fork failed!\n");
//      exit(1);
//    }
//
//    if (ppid == 0) {
//      close(pipe_fd[READ_END]);     // The end who wants to write (the child)
//      dup2(pipe_fd[WRITE_END], 1);  // Set the output stream to the pipe write end
//      process_pgm(pgm->next);
//    } else {
//      close(pipe_fd[WRITE_END]);  // The end who wants to read (the parent)
//      dup2(pipe_fd[READ_END], 0); // Set the input stream to the pipe read end
//    }
//  }
//  // Parent (maybe without child)
//  execvp(pname, pname_args);
//  fprintf(stderr, "lsh: command not found: %s\n", pname);
//  exit(1);
//}

/* 
 * Print a Comcand structure as returned by parse on stdout. 
 * 
 * Helper function, no need to change. Might be useful to study as inpsiration.
 */
void DebugPrintCommand(int parse_result, Command *cmd)
{
  if (parse_result != 1) {
    printf("Parse ERROR\n");
    return;
  }
  printf("------------------------------\n");
  printf("Parse OK\n");
  printf("stdin:      %s\n", cmd->rstdin ? cmd->rstdin : "<none>");
  printf("stdout:     %s\n", cmd->rstdout ? cmd->rstdout : "<none>");
  printf("background: %s\n", cmd->background ? "true" : "false");
  printf("Pgms:\n");
  PrintPgm(cmd->pgm);
  printf("------------------------------\n");
}


/* Print a (linked) list of Pgm:s.
 * 
 * Helper function, no need to change. Might be useful to study as inpsiration.
 */
void PrintPgm(Pgm *p)
{
  if (p == NULL)
  {
    return;
  }
  else
  {
    char **pl = p->pgmlist;

    /* The list is in reversed order so print
     * it reversed to get right
     */
    PrintPgm(p->next);
    printf("            * [ ");
    while (*pl)
    {
      printf("%s ", *pl++);
    }
    printf("]\n");
  }
}


/* Strip whitespace from the start and end of a string. 
 *
 * Helper function, no need to change.
 */
void stripwhite(char *string)
{
  register int i = 0;

  while (isspace(string[i]))
  {
    i++;
  }

  if (i)
  {
    memmove(string, string + i, strlen(string + i) + 1);
  }

  i = strlen(string) - 1;
  while (i > 0 && isspace(string[i]))
  {
    i--;
  }

  string[++i] = '\0';
}


// Assumes environemnt is not too big
void printenv()
{
  int i = 0;
  while (environ[i]) {
    printf("%s\n", environ[i]);
    i++;
  }
}

