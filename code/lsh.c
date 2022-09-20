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

extern char **environ;

int main(void)
{
  Command cmd;
  int parse_result;

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


/* Execute the given command(s).

 * Note: The function currently only prints the command(s).
 * 
 * TODO: 
 * 1. Implement this function so that it executes the given command(s).
 * 2. Remove the debug printing before the final submission.
 */

void RunCommand(int parse_result, Command *cmd) {
  DebugPrintCommand(parse_result, cmd);
  int pid = fork();
  if (pid == 0) {
    // Child process (fork)
    process_pgm(cmd->pgm);
  } else {
    // Parent process (shell)
    wait(NULL);
  }
}

void process_pgm(Pgm *pgm) {
  char *pname =       pgm->pgmlist[0];
  char **pname_args = pgm->pgmlist;
  char **pargs =      pgm->pgmlist + 1;

  // Shell commands (exit, cd)
  if (!strcmp(pname, "exit")) {
    printf("goodbye! 👋");
    exit(0);
  } else if (!strcmp(pname, "cd")) {
    chdir(pargs[0]);
    return;
  }

  if (pgm->next != NULL) {
    // Create pipe, then fork
    int pipe_fd[2];

    if (pipe(pipe_fd) == -1) {
      fprintf(stderr,"Pipe failed");
      exit(1);
    }

    int ppid = fork();
    if (ppid == -1) {
      printf("pipe-fork failed!\n");
      exit(1);
    }

    if (ppid == 0) {
      // The end who wants to write (the child)
      close(pipe_fd[READ_END]);
      // Set the output stream to the pipe write end
      dup2(pipe_fd[WRITE_END], 1);
      process_pgm(pgm->next);
    } else {
      // The end who wants to read (the parent)
      close(pipe_fd[WRITE_END]);
      // Set the input stream to the pipe read end
      dup2(pipe_fd[READ_END], 0);
      // Execute the reading process process
      execvp(pname, pname_args);
    }
  } else {
    execvp(pname, pname_args);
  }
}

//void RunCommand(int parse_result, Command *cmd)
//{
//  Pgm *current_pgm = cmd->pgm;
//  while (current_pgm != NULL) {
//    char *pname = current_pgm->pgmlist[0];
//    char **pname_args = current_pgm->pgmlist;
//    char **pargs = current_pgm->pgmlist + 1;
//
//    //First, handle special commands (exit, cd)
//    if (!strcmp(pname, "exit")) {
//      printf("goodbye! 👋");
//      exit(0);
//    } else if (!strcmp(pname, "cd")) {
//      chdir(pargs[0]);
//      return;
//    }
//
//    // Create process
//    int pid;
//    DebugPrintCommand(parse_result, cmd);
//    pid = fork();
//    if (pid == -1) {
//      printf("could not fork current process!\n");
//      exit(1);
//    }
//    if (pid == 0) {
//      // If other elements in linked-list, create pipe and fork again
//      if (current_pgm->next != NULL) {
//        int pipe_fd[2];
//
//        if (pipe(pipe_fd) == -1) {
//          fprintf(stderr,"Pipe failed");
//          exit(1);
//        }
//
//        int ppid = fork();
//        if (ppid == -1) {
//          printf("pipe-fork failed!\n");
//          exit(1);
//        }
//
//        if (ppid == 0) {
//          // The end who wants to write (the child)
//          close(pipe_fd[READ_END]);
//          // Set the output stream to the pipe write end
//          dup2(pipe_fd[WRITE_END], 1);
//        } else {
//          // The end who wants to read (the parent)
//          close(pipe_fd[WRITE_END]);
//          // Set the input stream to the pipe read end
//          dup2(pipe_fd[READ_END], 0);
//          // Execute the reading process process
//          execvp(pname, pname_args);
//        }
//      }
//    } else {
//      // Parent prosess (shell)
//      wait(NULL);
//    }
//
//    // Next command
//    current_pgm = current_pgm->next;
//  }
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

