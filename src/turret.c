#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include <ncurses.h>
#include <pigpiod_if2.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <pthread.h>

#define SERVO_MIN 500
#define SERVO_MAX 2500

int32_t PIGPIO_HANDLE = -1;

int32_t PIN_X = -1;
int32_t PIN_Y = -1;

int32_t FLIP_X = 0;
int32_t FLIP_Y = 0;

int32_t SENTINAL = 1;
pthread_mutex_t mSENTINAL = PTHREAD_MUTEX_INITIALIZER;

void (*previous_handler)(int32_t) = NULL;

const int32_t PWM_RANGE = SERVO_MAX - SERVO_MIN;

int32_t update_x (double position);
int32_t update_y (double position);

int32_t main (int32_t argc, char* argv[]) {

  if (argc < 3) {
    fprintf(stderr, "%s\n", "Usage:\nturret <pin-x> <pin-y>");
    return EINVAL;
  }

  int64_t tmp = LONG_MIN;
  
  tmp = strtol(argv[1], NULL, 10);

  if (tmp > 0L && tmp <= 28L)
    PIN_X = (int32_t)tmp;
  else {
    fprintf(stderr, "%s: %s is not a valid pin for X\n", strerror(EINVAL), argv[1]);
    fflush(stderr);
    return EINVAL;
  }

  tmp = strtol(argv[2], NULL, 10);

  if (tmp > 0L && tmp <= 28L)
    PIN_Y = (int32_t)tmp;
  else {
    fprintf(stderr, "%s: %s is not a valid pin for Y\n", strerror(EINVAL), argv[2]);
    fflush(stderr);
    return EINVAL;
  }

  
  PIGPIO_HANDLE = pigpio_start(NULL, NULL);

  if (PIGPIO_HANDLE < 0) {
    fprintf(stderr, "%s\n", pigpio_error(PIGPIO_HANDLE));
    return EIO;
  }

  initscr();
  start_color();

  cbreak();
  keypad(stdscr, TRUE);
  noecho();

  init_pair(1, COLOR_BLACK, COLOR_CYAN);
  init_pair(2, COLOR_BLACK, COLOR_GREEN);
  init_pair(3, COLOR_BLACK, COLOR_MAGENTA);

  wattron(stdscr, COLOR_PAIR(1));
  mvwprintw(stdscr, 0, 0, "Press F1 to Invert X");
  wattroff(stdscr, COLOR_PAIR(1));

  wattron(stdscr, COLOR_PAIR(2));
  mvwprintw(stdscr, 1, 0, "Press F2 to Invert Y");
  wattroff(stdscr, COLOR_PAIR(2));

  wattron(stdscr, COLOR_PAIR(3));
  mvwprintw(stdscr, 2, 0, "Press F3 to Swap pin assignments");
  wattroff(stdscr, COLOR_PAIR(3));

  wrefresh(stdscr);

  int32_t max_x, max_y, cursor_x, cursor_y;

  getmaxyx(stdscr, max_y, max_x);

  WINDOW* position_window = newwin(max_y - 8, max_x - 2, 4, 1);
  box(position_window, 0, 0);

  cursor_x = (max_x - 2) / 2;
  cursor_y = (max_y - 8) / 2;

  wattron(position_window, COLOR_PAIR(1));
  mvwprintw(position_window, cursor_y, cursor_x, " ");
  wattroff(position_window, COLOR_PAIR(1));

  update_x(0.5);
  update_y(0.5);

  wrefresh(position_window);

  int32_t _sentinal_ = SENTINAL;
  int32_t swap_x = -1;

  while (_sentinal_) {
    
    int32_t pressed = getch();

    int32_t new_x = cursor_x, new_y = cursor_y;

    mvwprintw(position_window, cursor_y, cursor_x, " ");
    
    switch (pressed) {
      case 'a':
      case KEY_LEFT:
        if (cursor_x - 1 > 0) new_x = cursor_x - 1;
        break;
      case 'd':
      case KEY_RIGHT:
        if(cursor_x + 1 < (max_x - 2)) new_x = cursor_x + 1;
        break;
      case 'w':
      case KEY_UP:
        if (cursor_y - 1 > 0) new_y = cursor_y - 1;
        break;
      case 's':
      case KEY_DOWN:
        if (cursor_y + 1 < (max_y - 8)) new_y = cursor_y + 1;
        break;
      case KEY_F(1):
          FLIP_X = !FLIP_X;
          update_x(new_x / (max_x - 2.0));
        break;
      case KEY_F(2):
          FLIP_Y = !FLIP_Y;
          update_y(new_y / (max_y - 8.0));
        break;
      case KEY_F(3):
        swap_x = PIN_X;
        PIN_X = PIN_Y;
        PIN_Y = swap_x;
        update_x(new_x / (max_x - 2.0));
        update_y(new_y / (max_y - 8.0));
        break;
      default:
        break;
    }

    if (new_x != cursor_x) {
      int32_t err = update_x(new_x / (max_x - 2.0));
      if (err != EXIT_SUCCESS)
        fprintf(stderr, "%s\n", err < 0 ? pigpio_error(err) : strerror(err));
    }


    if (new_y != cursor_y) {
      int32_t err = update_y(new_y / (max_y - 8.0));
      if (err != EXIT_SUCCESS)
        fprintf(stderr, "%s\n", err < 0 ? pigpio_error(err) : strerror(err));
    }

    cursor_x = new_x;
    cursor_y = new_y;

    wattron(position_window, COLOR_PAIR(1));
    mvwprintw(position_window, cursor_y, cursor_x, " ");
    wattroff(position_window, COLOR_PAIR(1));

    wrefresh(stdscr);
    wrefresh(position_window);

    if (pthread_mutex_lock(&mSENTINAL) == EXIT_SUCCESS) {
      _sentinal_ = SENTINAL;
      pthread_mutex_unlock(&mSENTINAL);
    }

  }

  endwin();

  return EXIT_SUCCESS;
}


void interrupted (int32_t signum) {

  if (pthread_mutex_lock(&mSENTINAL) != EXIT_SUCCESS) {
    fprintf(stderr, "%s: Error, shouldn't be here\n", strerror(EBUSY));
  }

  SENTINAL = 0;
  pthread_mutex_unlock(&mSENTINAL);

  if (PIGPIO_HANDLE >= 0) {
    if (PIN_X > 0 && PIN_X <= 28)
      set_servo_pulsewidth(PIGPIO_HANDLE, PIN_X, 0);
    if (PIN_Y > 0 && PIN_Y <= 28)
      set_servo_pulsewidth(PIGPIO_HANDLE, PIN_Y, 0);

    pigpio_stop(PIGPIO_HANDLE);
  }

  
  signal(signum, previous_handler != NULL ? previous_handler : SIG_DFL);
 
  if(previous_handler != NULL && previous_handler != SIG_DFL && previous_handler != SIG_IGN) 
    previous_handler(signum);

}

int32_t update_x (double x_position) {
  if (x_position < 0.0 || x_position > 1.0) return EINVAL;
  if (PIGPIO_HANDLE < 0) return EIO;

  int32_t status = EXIT_SUCCESS;

  if (FLIP_X) x_position = 1.0 - x_position;

  int32_t pulsewidth = (int32_t)(500.0 + (PWM_RANGE * x_position));

  if (pulsewidth >= SERVO_MIN && pulsewidth <= SERVO_MAX)
    status = set_servo_pulsewidth(PIGPIO_HANDLE, PIN_X, pulsewidth);
  
  return status;
}


int32_t update_y (double y_position) {
  if (y_position < 0.0 || y_position > 1.0) return EINVAL;
  if (PIGPIO_HANDLE < 0) return EIO;

  int32_t status = EXIT_SUCCESS;

  if (FLIP_Y) y_position = 1.0 - y_position;

  int32_t pulsewidth = (int32_t)(500.0 + (PWM_RANGE * y_position));

  if (pulsewidth >= SERVO_MIN && pulsewidth <= SERVO_MAX)
    status = set_servo_pulsewidth(PIGPIO_HANDLE, PIN_Y, pulsewidth);
  
  return status;
}
