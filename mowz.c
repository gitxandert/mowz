#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define KEY_DEPRESSED 1
#define KEY_RELEASED 0
#define KEY_REPEAT 2

static void write_to_device(int, struct input_event, int, int, int);
static void print_help();

const char* PID_FILE = "/run/mowz.pid";

int main(int argc, char *argv[]) {
  if (argc == 1) {
    print_help();
    exit(EXIT_SUCCESS);
  } 
  if (strcmp(argv[1], "help") == 0) {
    print_help();
    exit(EXIT_SUCCESS); 
  }
  if (strcmp(argv[1], "start") != 0 && strcmp(argv[1], "stop") != 0) {
    fprintf(stderr, "mowz: invalid command '%s' (run 'mowz' or 'mowz help' for list of commands and bindings)\n", argv[1]);
    exit(EXIT_FAILURE);
  }

  if (geteuid() != 0) {
    fprintf(stderr, "mowz: run as root\n");
    exit(EXIT_FAILURE);
  }

  if (strcmp(argv[1], "stop") == 0) {
    FILE *mowz_fp;
    if ((mowz_fp = fopen(PID_FILE, "r")) == NULL) {
      fprintf(stderr, "mowz: not running\n");
      exit(EXIT_FAILURE);
    }

    char buf[128];
    if (fgets(buf, 128, mowz_fp) == NULL) {
      fprintf(stderr, "mowz: nothing in %s\n", PID_FILE);
      fclose(mowz_fp);
      unlink(PID_FILE);
      exit(EXIT_FAILURE);
    }

    int mowz_pid = atoi(buf);
    if (kill(mowz_pid, SIGTERM) != 0) {
      fprintf(stderr, "mowz: error terminating %s\n", PID_FILE);
      fclose(mowz_fp);
      unlink(PID_FILE);
      exit(EXIT_FAILURE);
    }
     
    fclose(mowz_fp);
    unlink(PID_FILE);

    printf("mowz: stopped\n");

    exit(EXIT_SUCCESS);
  }

  FILE *already_started;
  if ((already_started = fopen(PID_FILE, "r")) != NULL) {
    fprintf(stderr, "mowz: already running\n");
    fclose(already_started);
    exit(EXIT_FAILURE);
  }

  int               i, fd0, fd1, fd2;
  pid_t             pid;
  struct rlimit     rl;
  struct sigaction  sa;

  umask(0);

  if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
    fprintf(stderr, "mowz: can't get file limit\n");
    exit(EXIT_FAILURE);
  }

  if ((pid = fork()) < 0) {
    fprintf(stderr, "mowz: can't fork\n");
    exit(EXIT_FAILURE);
  } else if (pid != 0) {
    printf("mowz: starting (run `journalctl -t mowz` for syslogs)\n");
    exit(0);
  }

  setsid();

  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGHUP, &sa, NULL) < 0) {
    fprintf(stderr, "mowz: can't ignore SIGHUP\n");
    exit(EXIT_FAILURE);
  }
  if ((pid = fork()) < 0) {
    fprintf(stderr, "mowz: can't fork\n");
    exit(EXIT_FAILURE);
  } else if (pid != 0) {
    exit(0);
  }

  if (chdir("/") < 0) {
    fprintf(stderr, "mowz: can't change directory to /\n");
    exit(EXIT_FAILURE);
  }

  if (rl.rlim_max == RLIM_INFINITY)
    rl.rlim_max = 1024;
  for (i = 0; i < rl.rlim_max; i++)
    close(i);

  fd0 = open("/dev/null", O_RDWR);
  fd1 = dup(0);
  fd2 = dup(0);

  openlog("mowz", LOG_CONS, LOG_DAEMON);
  if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
    syslog(LOG_ERR, "at least one unexpected file descriptor: %d %d %d",
        fd0, fd1, fd2);
    exit(EXIT_FAILURE);
  }

  int fd_kbd, fd_uin;
  FILE *kbd_stream;
  struct input_event kbd_input, mowz;
  
  //event3 = platform-i8042-serio-0-event-kbd
  if ((fd_kbd = open("/dev/input/event3", O_RDONLY)) < 0) {
    syslog(LOG_ERR, "unable to open keyboard file descriptor");
    exit(EXIT_FAILURE);
  }
  if ((kbd_stream = fdopen(fd_kbd, "rb")) == NULL) {
    syslog(LOG_ERR, "unable to open kbd stream");
    exit(EXIT_FAILURE);
  }

  //create virtual device for mouse movement
  if((fd_uin = open("/dev/uinput", O_WRONLY | O_NONBLOCK)) < 0) {
    syslog(LOG_ERR, "unable to open uinput file descriptor");
    exit(EXIT_FAILURE);
  }

  ioctl(fd_uin, UI_SET_EVBIT, EV_REL);
  ioctl(fd_uin, UI_SET_EVBIT, EV_KEY);

  ioctl(fd_uin, UI_SET_RELBIT, REL_X);
  ioctl(fd_uin, UI_SET_RELBIT, REL_Y);

  ioctl(fd_uin, UI_SET_RELBIT, REL_WHEEL);
  ioctl(fd_uin, UI_SET_RELBIT, REL_HWHEEL);

  ioctl(fd_uin, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(fd_uin, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(fd_uin, UI_SET_KEYBIT, BTN_MIDDLE);
  
  struct uinput_setup setup;
  memset(&setup, 0, sizeof(setup));

  snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "mowz");
  setup.id.bustype = BUS_USB;
  setup.id.vendor = 0x1;
  setup.id.product = 0x1;
  setup.id.version = 1;

  ioctl(fd_uin, UI_DEV_SETUP, &setup);
  ioctl(fd_uin, UI_DEV_CREATE);

  //create PID file
  pid_t mowz_pid = getpid();
  FILE *mowz_fp = fopen(PID_FILE, "w");
  fprintf(mowz_fp, "%d\n", mowz_pid);
  fclose(mowz_fp);

  //loop and read from keyboard
  int grab = 0;
  int grab_ctl = 0;
  int grab_shift = 0;
  int step = 10;
  while (1) {
    if (fread(&kbd_input, sizeof(struct input_event), 1, kbd_stream) != 1)
      continue;

    int code = kbd_input.code;
    int value = kbd_input.value;

    memset(&mowz, 0, sizeof(mowz));

    switch (code) {
      case KEY_LEFTCTRL: {
        if (value == KEY_DEPRESSED) {
          grab_ctl = 1;
          step = 50;
        } else if (value == KEY_RELEASED) {
          grab_ctl = 0;
          step = 10;
        }
        break;
      }
      case KEY_LEFTALT: {
        if (value == KEY_DEPRESSED) {
          grab_shift = 1;
        } else if (value == KEY_RELEASED) {
          grab_shift = 0;
        }
        break;
      }
      case KEY_M: {
        if (value == KEY_DEPRESSED) {
          if (grab_ctl == 1 && grab_shift == 1) {
            if (grab == 0) {
              grab = 1;
            } else {
              grab = 0;
            }
          }
        } else if (value == KEY_RELEASED) {
          ioctl(fd_kbd, EVIOCGRAB, grab);
        }
        break;
      }
      default: break;
    }

    if (grab == 1) {
      if (value == KEY_DEPRESSED || value == KEY_REPEAT) {
        switch (code) {
          case KEY_H: {
            // to x = 0
            write_to_device(fd_uin, mowz, EV_REL, REL_X, -step);
            break;
          }
          case KEY_U: {
            // to origin
            write_to_device(fd_uin, mowz, EV_REL, REL_X, -step);
            write_to_device(fd_uin, mowz, EV_REL, REL_Y, -step);
            break;
          }
          case KEY_J: {
            // to y = MAX
            write_to_device(fd_uin, mowz, EV_REL, REL_Y, step);
            break;
          }
          case KEY_I: {
            // to (x, y) = (MAX, 0)
            write_to_device(fd_uin, mowz, EV_REL, REL_X, step);
            write_to_device(fd_uin, mowz, EV_REL, REL_Y, -step);
            break;
          }
          case KEY_K: {
            // to y = 0
            write_to_device(fd_uin, mowz, EV_REL, REL_Y, -step);
            break;
          }
          case KEY_L: {
            // to x = SCREEN_WIDTH
            write_to_device(fd_uin, mowz, EV_REL, REL_X, step);
            break;
          }
          case KEY_M: {
            // to (x, y) = (MAX, MAX)
            write_to_device(fd_uin, mowz, EV_REL, REL_X, step);
            write_to_device(fd_uin, mowz, EV_REL, REL_Y, step);
            break;
          }
          case KEY_N: {
            // to (x, y) = (0, MAX)
            write_to_device(fd_uin, mowz, EV_REL, REL_X, -step);
            write_to_device(fd_uin, mowz, EV_REL, REL_Y, step);
            break;
          }
          case KEY_SPACE: {
            // left-click
            write_to_device(fd_uin, mowz, EV_KEY, BTN_LEFT, KEY_DEPRESSED);
            break;
          }
          case KEY_RIGHTALT: {
            // right-click
            write_to_device(fd_uin, mowz, EV_KEY, BTN_RIGHT, KEY_DEPRESSED);
            break;
          }
          case KEY_Y: {
            // scroll up
            write_to_device(fd_uin, mowz, EV_REL, REL_WHEEL, 1);
            break;
          }
          case KEY_B: {
            // scroll down
            write_to_device(fd_uin, mowz, EV_REL, REL_WHEEL, -1);
            break;
          }
          case KEY_COMMA: {
            write_to_device(fd_uin, mowz, EV_REL, REL_HWHEEL, -1);
            break;
          }
          case KEY_DOT: {
            write_to_device(fd_uin, mowz, EV_REL, REL_HWHEEL, 1);
            break;
          }
          default: continue;
        }
      } else if (value == KEY_RELEASED) {
        switch(code) {
          case KEY_SPACE: {
            write_to_device(fd_uin, mowz, EV_KEY, BTN_LEFT, KEY_RELEASED);
            break;
          }
          case KEY_RIGHTALT: {
            write_to_device(fd_uin, mowz, EV_KEY, BTN_RIGHT, KEY_RELEASED);
            break;
          }
          default: break;
        }
      }
    }
  }

  ioctl(fd_uin, UI_DEV_DESTROY);
  close(fd_uin);

  exit(EXIT_SUCCESS);
}

static void write_to_device(int fd, struct input_event ev, int type, int code, int value) {
  ev.type = type;
  ev.code = code;
  ev.value = value;

  if (write(fd, &ev, sizeof(ev)) != sizeof(ev))
    syslog(LOG_ERR, "write error");

  memset(&ev, 0, sizeof(ev));
  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  if (write(fd, &ev, sizeof(ev)) != sizeof(ev))
    syslog(LOG_ERR, "sync error");
}

static void print_help() {
  printf("    mowz: friendly mouse movement with keyboard input\n\n");
  printf("    --commands:\n");
  printf("\tsudo mowz start: start mowz daemon\n");
  printf("\tsudo mowz stop: stop mowz daemon\n");
  printf("\tmowz / mowz help: show mowz commands and bindings\n\n");
  printf("    --bindings:\n");
  printf("\tleft_ctl + left_alt + m: toggle keyboard grabbing\n");
  printf("\tleft_ctl (hold): increase step factor\n");
  printf("\th: move left\n");
  printf("\tj: move down\n");
  printf("\tk: move up\n");
  printf("\tl: move right\n");
  printf("\tu: move upper left\n");
  printf("\ti: move upper right\n");
  printf("\tn: move lower left\n");
  printf("\tm: move lower right\n");
  printf("\tspacebar: right-click\n");
  printf("\n\t(left_ctl does not interact with the following commands)\n");
  printf("\n\tright_alt: left-click\n");
  printf("\ty: scroll up\n");
  printf("\tb: scroll down\n");
  printf("\t,: scroll left\n");
  printf("\t.: scroll right\n\n");
  printf("    --tips\n");
  printf("\t--avoid focusing in a window when grabbing the keyboard\n");
  printf("\t--you can spacebar + move, though it's a little jerky\n");
}
