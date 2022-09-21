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

void process_pgm();
void printenv();
void catch_sigint(int signum);
void catch_sigchld(int signum);

extern char **environ;

int fgpid = 0;


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


// Execute the given command(s).
void RunCommand(int parse_result, Command *cmd) {
  //DebugPrintCommand(parse_result, cmd);

  // Shell commands (exit, cd)
  char *pname = cmd->pgm->pgmlist[0];
  char **pargs = cmd->pgm->pgmlist + 1;
  if ((!strcmp(pname, "exit")) || (!strcmp(pname, ":q"))) {
    exit(0);
  } else if (!strcmp(pname, "cd")) {
    chdir(pargs[0]);
    return;
  }

  // Before fork, make sure that if background process, don't recieve sigint
  int pid = fork();
  if (pid == 0) {
    // Child process (fork)
    // Handle input/output file redirection
    if (cmd->rstdout != NULL) {
      // Create new file and connect stdout
      int fd = open(cmd->rstdout, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (fd == -1) {
        printf("could not open file: %s\n", cmd->rstdout);
      } else {
        dup2(fd, 1);
      }
    }
    if (cmd->rstdin != NULL) {
      // Create new file and connect stdin
      int fd = open(cmd->rstdin, O_RDONLY, O_RDWR);
      if (fd == -1) {
        printf("could not read file: %s\n", cmd->rstdin);
      } else {
        dup2(fd, 0);
      }
    }
    process_pgm(cmd->pgm);
  } else {
    // Parent process (shell)
    if (cmd->background) {
      //printf("you are free my process!");
      // Set group pid of background process to something elses so it does not
      // react to SIGINT
      setpgid(pid, pid);
    } else {
      fgpid = pid;
      // Wait until fgpid has been resolved by singal hanlder
      while (fgpid != 0) {
        pause();
      }
      fgpid = 0;
    }
  }
}

// Recursive function to hanlde processes
void process_pgm(Pgm *pgm) {
  char *pname =       pgm->pgmlist[0];
  char **pname_args = pgm->pgmlist;

  if (pgm->next != NULL) {
    // Create pipe, then fork
    int pipe_fd[2];

    if (pipe(pipe_fd) == -1) {
      fprintf(stderr,"pipe failed!\n");
      exit(1);
    }

    int ppid = fork();
    if (ppid == -1) {
      printf("pipe-fork failed!\n");
      exit(1);
    }

    if (ppid == 0) {
      close(pipe_fd[READ_END]);     // The end who wants to write (the child)
      dup2(pipe_fd[WRITE_END], 1);  // Set the output stream to the pipe write end
      process_pgm(pgm->next);
    } else {
      close(pipe_fd[WRITE_END]);  // The end who wants to read (the parent)
      dup2(pipe_fd[READ_END], 0); // Set the input stream to the pipe read end
    }
  }
  // Parent (maybe without child)
  execvp(pname, pname_args);
  printf("lsh: command not found: %s\n", pname);
  exit(1);
}


// INT (Ctrl-C)
void catch_sigint(int signum) {
  // Set function to handle signal again
  signal(SIGINT, catch_sigint);
  printf("signum: %i\n", signum == SIGINT);
  printf("\n> ");

  // We do not need to send a sigint to the child process in the foregorund,
  // we need to make sure it does not reach processes in the background!
}


// CHILD (when child terminates and other things)
void catch_sigchld(int signum) {
  // Wait will fail if foreground process, since we have already waited
  int child_pid = waitpid(-1, NULL, WNOHANG);
  // Return of wait is equal to current_pid
  if (child_pid == fgpid) {
    fgpid = 0;
    // Foreground process
    //printf("foreground process terminated: %i\n", child_pid);
  } else {
    // Background process
    printf("background process terminated: %i\n", child_pid);
  }
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

