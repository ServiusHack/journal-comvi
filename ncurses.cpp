#include <ncurses.h>
#include <locale.h>

#include "ncurses.h"

ncurses::ncurses()
{
  setlocale(LC_ALL, "");
  initscr();
  start_color();
  scrollok(stdscr, TRUE);

  if (has_colors())
  {
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);
    init_pair(3, COLOR_BLUE, COLOR_BLACK);
  }

  wprintw(stdscr, "Starting ...");
  refresh();
}

ncurses::~ncurses()
{
  endwin();
}
