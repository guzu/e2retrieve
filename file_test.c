#include <stdlib.h>
#include <signal.h>
#include <errno.h>

/*
void child_handler(int signum) {
  int pid;

  pid = wait(NULL);
}
*/

int main(int argc, char* argv[]) {
  char *cmd[4] = { "file", "-b", NULL, NULL };
  int pfd[2], n;
  char ch;

  cmd[2] = argv[1];

  if(pipe(pfd) == -1) {
    perror("pipe");
    exit(1);
  }

  switch(fork()) {
  case 0:
    close(pfd[0]);
    close(1); dup(pfd[1]);
    execvp(cmd[0], cmd);
  case -1:
    break;
  default:
    close(pfd[1]);

    /* signal(SIGCHLD, child_handler); */

    while(read(pfd[0], &ch, 1) > 0) {
      write(1, &ch, 1);
    }
    wait(NULL);
  }

  return 1;
}
