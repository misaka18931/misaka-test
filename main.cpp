#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <string>
#include <boost/interprocess/anonymous_shared_memory.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include "argparse-2.5/include/argparse/argparse.hpp"
#include <utility>
using std::tuple;
using std::string;
using std::pair;
const time_t time_margin = 200;
#define err(...) fprintf(stderr, __VA_ARGS__)

inline timeval to_timeval(const time_t &t) {
  return {t / 1000, t % 1000 * 1000};
}

namespace itimer {
static int runner_pid;
static bool timeout;
static void itimer_handler(int) {
  kill(runner_pid, SIGTERM);
  timeout = 1;
}
const struct sigaction timer_action{ .__sigaction_handler{itimer_handler}};
inline int timer_setup(int _pid, time_t tl) {
  runner_pid = _pid;
  itimerval real_timer = {{0l, 0l}, to_timeval(tl + time_margin)};
  if (setitimer(ITIMER_REAL, &real_timer, nullptr) == -1) {
    perror("runner timer [3]");
    return -1;
  }
  return sigaction(SIGALRM, &itimer::timer_action, nullptr);
}
}

class pipe_ipc {
  int fds[2];
public:
  pipe_ipc() {
    if (pipe(fds) == -1) {
      perror("pipe init");
      exit(EXIT_FAILURE);
    }
  }
  template<typename T> int send(const T &data) {
    return write(fds[1], &data, sizeof (T));
  }
  template<typename T> int recv(T &data) {
    return read(fds[0], &data, sizeof (T));
  }
};

template<typename T>
class shared_object {
  boost::interprocess::mapped_region region;
public:
  shared_object() {
    region = boost::interprocess::anonymous_shared_memory(sizeof(T));
    T def;
    memcpy(region.get_address(), &def, sizeof(T));
  }
  T value() {
    T ret{};
    memcpy(&ret, region.get_address(), sizeof(T));
    return ret;
  }
  void set(const T& val) {
    memcpy(region.get_address(), &val, sizeof(T));
  }
};

enum status {
  success, killed, failed
};

inline int compile();

tuple<status, time_t, size_t> run_normal_no_redirect(const string& prog, const time_t &tl = 0) {
  shared_object<bool> flag;
  shared_object<pair<rusage, int> > report;
  pid_t pid = fork();
  if (pid == -1) {
    perror("runner fork [1]");
    return {status::failed, 0, 0};
  } else if (pid == 0) { // timer wrapper
    pid_t progpid = fork();
    if (progpid == -1) {
      perror("runner fork [2]");
      flag.set(1);
      exit(1);
    } else if (progpid == 0) { // solution
      /*
      rlimit mem_lim = {ml << 10, ml << 10}; // in bytes
      if (ml > 0) {
        if (setrlimit(RLIMIT_DATA, &mem_lim) == -1) {
          perror("runner rlimit [4]");
          flag.set(1);
          exit(1);
        }
      }
      */
      execv(prog.c_str(), nullptr);
      perror("runner exec [5]");
      flag.set(1);
      exit(1);
    } else {
      if (tl > 0) if (itimer::timer_setup(progpid, tl) == -1) {
        flag.set(1);
        kill(progpid, SIGKILL);
        exit(1);
      }
      int ret;
      rusage info;
      wait3(&ret, 0, &info);
      report.set({info, ret});
      exit(0);
    }
  } else {
    wait(nullptr);
    if (flag.value()) {
      return {status::failed, 0, 0};
    }
    auto [ru_stat, ret] = report.value();
    if (!WIFEXITED(ret) || WEXITSTATUS(ret) != 0) return {status::killed, 0, 0};
    time_t elapsed_time = ru_stat.ru_utime.tv_sec * 1000 + ru_stat.ru_utime.tv_usec / 1000;
    return {status::success, elapsed_time, ru_stat.ru_maxrss};
  }
}

int main(int argc, char **argv) {
  argparse::ArgumentParser ap("misaka-test");
  ap.add_argument("problem");
  ap.add_argument("time_limit").scan<'d', int>();
  ap.add_argument("memory_limit");
  ap.add_argument("-if")
    .help("path to input")
    .default_value("../data/$problem/$problem$i.in");
  ap.add_argument("-of")
    .help("path to output")
    .default_value("../data/$problem/$problem$i.out");
  try {
    ap.parse_args(argc, argv);
  } catch (const std::runtime_error& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << ap;
    std::exit(1);
  }
  
  return 0;
}
