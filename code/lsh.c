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
int is_built_in_command(char **pgmlist);
int fork_failed(int pid);
void catch_sigint(int signum);
void catch_sigchld(int signum);

extern char **environ;

Command *current_cmd = NULL;


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
  if (parse_result == -1) {
    fprintf(stderr, "syntax error!\n");
    return;
  }
  current_cmd = cmd;

  // Two pipes, one for input of a process, one for output
  int in_fd[2] = {0,0};
  int out_fd[2] = {0,0};
  Pgm *p = cmd->pgm;

  // Shell commands (exit, cd), only does something in isolation
  if (p->next == NULL) {
    if ((!strcmp(p->pgmlist[0], "exit")) || (!strcmp(p->pgmlist[0], ":q"))) {
      exit(0);
    } else if (!strcmp(p->pgmlist[0], "cd")) {
      int err;
      if (p->pgmlist[1] == NULL) {
        err = chdir(getenv("HOME"));
      } else {
        err = chdir(p->pgmlist[1]);
      }
      if (err == -1) {
        fprintf(stderr, "cd: no such file or directory: %s", p->pgmlist[1]);
      }
      return;
    }
  }

  // Iterate and start all programs
  while (p != NULL) {
    // Switch read and write pipes if needed
    if (in_fd[0] != 0 || in_fd[1] != 0) {
      out_fd[READ_END] = in_fd[READ_END];
      out_fd[WRITE_END] = in_fd[WRITE_END];
      // Reset in
      in_fd[READ_END] = 0;
      in_fd[WRITE_END] = 0;
      //fprintf(stderr, "switching read and write: %i and %i\n", out_fd[0], out_fd[1]);
    }
    // Open read pipe if not the end
    if (p->next != NULL) {
      if (pipe(in_fd) == -1) {
        fprintf(stderr, "pipe failed!\n");
        exit(1);
      }
      //fprintf(stderr, "opening new read pipe: %i and %i\n", in_fd[0], in_fd[1]);
    }
    // Start fork
    int pid = fork();
    if (fork_failed(pid)) { exit(1); }
    if (pid == 0) {
      // Child process, can connect to both read and write end (if there are more than one pipd)
      if (in_fd[READ_END] != 0 || in_fd[WRITE_END] != 0) {
        // Read end
        //fprintf(stderr, "child process %i connects to read end of: %i and %i\n", getpid(), in_fd[0], in_fd[1]);
        close(in_fd[WRITE_END]);
        if (dup2(in_fd[READ_END], 0) == -1) {
          fprintf(stderr, "pipe read end dup2 failed for process %i\n", pid);
          exit(1);
        }
      }
      if (out_fd[READ_END] != 0 || out_fd[WRITE_END] != 0) {
        // Write end
        //fprintf(stderr, "child process %i connects to write end of: %i and %i\n", getpid(), out_fd[0], out_fd[1]);
        close(out_fd[READ_END]);
        //fprintf(stderr, "ABOUT TO ENTER DUP2 write\n");
        if (dup2(out_fd[WRITE_END], 1) == -1) {
          fprintf(stderr, "pipe write end dup2 failed for process %i\n", getpid());
          exit(1);
        }
        //fprintf(stderr, "IMPORTANT: at end of write-end of pipe: %i\n", getpid());
      }

      // Here the built in command is not allowed, just terminate
      if (is_built_in_command(p->pgmlist)) {
        exit(0);
      }
      handle_process(p, cmd);
    } else {
      // Parent process
      if (!cmd->background) { p->pid = pid; }  // Remember foreground process
      //fprintf(stderr, "pid catched by parent after fork: %i\n", pid);
      // Close parent's copies of file handlers
      if (out_fd[READ_END] != 0 || out_fd[WRITE_END] != 0) {
        close(out_fd[READ_END]);
        close(out_fd[WRITE_END]);
      }
      p = p->next;
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
  //printf("shell about to process next command, or return to readline\n");
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
      //fprintf(stderr, "could not open file: %s\n", cmd->rstdout);
    } else {
      dup2(fd, 1);
    }
  }

  // Input redirection
  if (pgm->next == NULL && cmd->rstdin) {
    // Create new file and connect stdin
    int fd = open(cmd->rstdin, O_RDONLY, O_RDWR);
    if (fd == -1) {
      //fprintf(stderr, "could not read file: %s\n", cmd->rstdin);
    } else {
      dup2(fd, 0);
    }
  }

  // Execute program or terminate
  execvp(pgm->pgmlist[0], pgm->pgmlist);
  fprintf(stderr, "lsh: command not found: %s\n", pgm->pgmlist[0]);
  exit(1);
}

int is_built_in_command(char **pgmlist) {
  return (!strcmp(pgmlist[0], "exit")) || (!strcmp(pgmlist[0], ":q")) || (!strcmp(pgmlist[0], "cd"));
}

// INT (Ctrl-C)
void catch_sigint(int signum) {
  // Set function to handle signal again
  signal(SIGINT, catch_sigint);
}


// CHILD (when child terminates and other things)
void catch_sigchld(int signum) {
  // Make sure to handle signal again
  signal(SIGCHLD, catch_sigchld);
  //fprintf(stderr, "inside << SIGCHLD >>\n");

  // Clean up all terminated processes by waiting
  int child_pid;
  while ((child_pid = waitpid(-1, NULL, WNOHANG)) > 0) {
    // Go through the linked list and see if it is a foreground process
    int is_fg_process = 0;
    int there_are_fg_processes = 0;
    Pgm *p = current_cmd->pgm;
    while (p != NULL) {
      if (p->pid != 0) {
        there_are_fg_processes = 1;
      }
      if (child_pid == p->pid) {
        is_fg_process = 1;
        // Resolve this pid
        p->pid = 0;
        break;
      }
      p = p->next;
    }

    if (is_fg_process) {
      //printf("foreground process terminated: %i\n", child_pid);
    } else {
      fprintf(stderr, "background process terminated: %i\n", child_pid);
      if (!there_are_fg_processes) {
        printf("> ");
        fflush(stdout);
      }
    }
  }
}

int fork_failed(int pid) {
  if (pid == -1) {
    fprintf(stderr, "fork failed!\n");
    return 1;
  }
  return 0;
}

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

