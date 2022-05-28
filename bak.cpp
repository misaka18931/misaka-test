#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <cstdio>
#include <cstring>

static void handler(int signum) {
  fprintf(stderr, "timer expired\n");
  kill(0, SIGTERM);
}

enum verdict {
  accepted,  wrong_answer, time_limit, memory_limit, runtime_error
};



int main(int argc, char **argv) {
  pid_t pid = fork();
  if (pid == -1) {
    fprintf(stderr, "fork failure.\n");
    return 1;
  } else if (pid == 0) {
    setsid();
    // timer
    itimerval virt_timer = {{0l, 0l}, {1l, 500000l}}; // 2500ms, one shot
    if (setitimer(ITIMER_REAL, &virt_timer, nullptr) == -1) {
      fprintf(stderr, "timer setup failure.\n");
      return 1;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigaction(SIGALRM, &sa,nullptr);
    pid_t se = fork();
    if (pid == -1) {
      fprintf(stderr, "second fork failure.\n");
      return 1;
    } else if (se == 0) {
      rlimit mem_lim = {3000000000,3000000000};
      setrlimit(RLIMIT_AS, &mem_lim);
      execv("test", argv);
    }
    int status;
    rusage usage;
    wait3(&status, 0, &usage);
    printf("%d %d.%06d\n", usage.ru_maxrss, usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
  } else 
  wait(nullptr);
  return 0;
}
