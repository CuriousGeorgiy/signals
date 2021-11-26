#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CRIT_SEC_BEGIN
#define CRIT_SEC_END

#define BUF_SZ 1024
static char buf[BUF_SZ];

static pid_t parent_pid;
static pid_t child_pid;

static sigset_t sigset;

const static struct timespec timeout = {
    .tv_sec = 60,
    .tv_nsec = 0,
};

void
sigquit_handler(int unused) {
  exit(EXIT_SUCCESS);
}

int
send_bit(bool bit) {
  int rc;
  CRIT_SEC_END
  if (bit) {
    rc = kill(parent_pid, SIGUSR2);
  } else {
    rc = kill(parent_pid, SIGUSR1);
  }
  if (rc != 0) {
    perror("SYSTEM ERROR: kill failed");
    return -1;
  }

  int signum = sigtimedwait(&sigset, NULL, &timeout);
  if (signum == -1) {
    perror("SYSTEM ERROR: sigtimedwait failed");
    return -1;
  }
  return 0;
  CRIT_SEC_BEGIN
}

int
send_confirm() {
  int rc = kill(child_pid, SIGUSR1);
  if (rc != 0) {
    perror("SYSTEM ERROR: kill failed");
    return -1;
  }
  return 0;
}

int
main(int argc, const char *const argv[]) {
  --argc;
  ++argv;

  if (argc != 1) {
    printf("USAGE: signal <path to file>\n");
    return EXIT_FAILURE;
  }

  parent_pid = getpid();

  const char *file_name = argv[0];
  int file_fd = open(file_name, O_RDONLY);
  if (file_fd == -1) {
    perror("SYSTEM ERROR: open failed");
    return EXIT_FAILURE;
  }

  int rc = sigemptyset(&sigset);
  if (rc != 0) {
    perror("SYSTEM ERROR: sigemptyset failed");
    return EXIT_FAILURE;
  }
  rc = sigaddset(&sigset, SIGUSR1);
  if (rc != 0) {
    perror("SYSTEM ERROR: sigaddset failed");
    return EXIT_FAILURE;
  }
  rc = sigaddset(&sigset, SIGUSR2);
  if (rc != 0) {
    perror("SYSTEM ERROR: sigaddset failed");
    return EXIT_FAILURE;
  }

  rc = sigprocmask(SIG_SETMASK, &sigset, NULL);
  if (rc != 0) {
    perror("SYSTEM ERROR: sigprocmask failed");
    return EXIT_FAILURE;
  }

  struct sigaction sa = {
      .sa_flags = 0,
      .sa_handler = SIG_IGN,
      .sa_restorer = NULL,
  };
  sigemptyset(&sa.sa_mask);
  rc = sigaction(SIGUSR1, &sa, NULL);
  if (rc != 0) {
    perror("SYSTEM ERROR: sigaction failed");
    return EXIT_FAILURE;
  }
  rc = sigaction(SIGUSR2, &sa, NULL);
  if (rc != 0) {
    perror("SYSTEM ERROR: sigaction failed");
    return EXIT_FAILURE;
  }
  sa.sa_handler = sigquit_handler;
  rc = sigaction(SIGQUIT, &sa, NULL);
  if (rc != 0) {
    perror("SYSTEM ERROR: sigaction failed");
    return EXIT_FAILURE;
  }

  child_pid = fork();
  if (child_pid == -1) {
    perror("SYSTEM ERROR: fork failed");
    return EXIT_FAILURE;
  } else if (child_pid == 0) {
    int signum = sigtimedwait(&sigset, NULL, &timeout);

    CRIT_SEC_BEGIN
    if (signum == -1) {
      if (errno == EAGAIN) {
        fprintf(stderr, "CLIENT ERROR: parent communication timeout\n");
      } else {
        perror("SYSTEM ERROR: sigtimedwait failed");
      }
      return EXIT_FAILURE;
    }
    while (true) {
      ssize_t bytes_read_cnt = read(file_fd, buf, BUF_SZ);
      if (bytes_read_cnt == -1) {
        perror("SYSTEM ERROR: read failed");
        return EXIT_FAILURE;
      }
      if (bytes_read_cnt == 0) {
        rc = kill(parent_pid, SIGQUIT);
        if (rc != 0) {
          perror("SYSTEM ERROR: kill failed");
          return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
      }
      for (size_t i = 0; i < bytes_read_cnt; ++i) {
        for (size_t j = 0; j < 8; ++j) {
          rc = send_bit(((buf[i] >> j) & 1) == 1);
          if (rc != 0) {
            return EXIT_FAILURE;
          }
        }
      }
    }
    CRIT_SEC_END
  } else {
    unsigned char ch = '\0';
    size_t i = 0;
    while (true) {
      rc = send_confirm();
      if (rc != 0) {
        return EXIT_FAILURE;
      }

      int signum = sigtimedwait(&sigset, NULL, &timeout);
      if (signum == -1) {
        perror("SYSTEM ERROR: sigtimedwait failed");
        return EXIT_FAILURE;
      }

      CRIT_SEC_BEGIN
      if (signum == SIGUSR2) {
        ch |= 1 << i;
      }
      if (i == 7) {
        rc = putchar(ch);
        if (rc != ch) {
          perror("SYSTEM ERROR: putc failed");
          return EXIT_FAILURE;
        }
        ch = i = 0;
      } else {
        ++i;
      }
      CRIT_SEC_END
    }
  }
}
