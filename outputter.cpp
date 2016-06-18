#include "outputter.h"

#include <ncurses.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/c_local_time_adjustor.hpp>

namespace
{
enum class Level
{
  emerg,
  alert,
  crit,
  err,
  warning,
  notice,
  info,
  debug
};
}

outputter::outputter(boost::asio::io_service& io_service)
  : io_service(io_service)
{
  getmaxyx(stdscr,row,col);
}

void outputter::add_line(const std::map<std::string, std::string>& values)
{
  static std::locale locale(std::cout.getloc(), new boost::posix_time::time_facet("%H:%M:%S"));

  boost::posix_time::ptime time;
  {
    auto it = values.find("__REALTIME_TIMESTAMP");
    if (it != values.end())
    {
      long timestamp = boost::lexical_cast<long>(it->second);
      time = boost::posix_time::ptime(boost::gregorian::date(1970,1,1) , boost::posix_time::microseconds(timestamp));
      using local_adj = boost::date_time::c_local_adjustor<boost::posix_time::ptime>;
      time = local_adj::utc_to_local(time);
    }
  }

  Level level = Level::notice;
  {
    auto it = values.find("PRIORITY");
    if (it != values.end())
    {
      if (it->second == "0")
        level = Level::emerg;
      else if (it->second == "1")
        level = Level::alert;
      else if (it->second == "2")
        level = Level::crit;
      else if (it->second == "3")
        level = Level::err;
      else if (it->second == "4")
        level = Level::warning;
      else if (it->second == "5")
        level = Level::notice;
      else if (it->second == "6")
        level = Level::info;
      else if (it->second == "7")
        level = Level::debug;
    }
  }

  std::string machine;
  {
    auto it = values.find("_HOSTNAME");
    if (it != values.end())
      machine = it->second;
    else
      machine = "<none>";
  }

  std::string process;
  {
    auto it = values.find("_COMM");
    if (it != values.end())
      process = it->second;
    else
      process = "kernel";
  }

  std::string message;
  {
    auto it = values.find("MESSAGE");
    if (it != values.end())
      message = it->second;
    else
      message = "<none>";
  }

  //TODO: Improve escpaing
  boost::replace_all(message, "\n", "\\n");

  if (errors_scroll_out > 0)
  {
    assert(all_above_errors_line > 0);
    wsetscrreg(stdscr, 0, row-1);
  }

  if (errors_scroll_out == 0 && all_above_errors_line == row-1)
  {
    return;
  }

  int color = 0;

  if (has_colors())
  {
    switch (level)
    {
      case Level::emerg:
      case Level::crit:
      case Level::err:
      case Level::alert:
        color = 1;
        break;
      case Level::warning:
        color = 2;
        break;
      case Level::info:
      case Level::notice:
        color = 0;
        break;
      case Level::debug:
        color = 3;
        break;
    }

  }

  if (color != 0)
  {
    attron(COLOR_PAIR(color));
  }

  std::stringstream time_stream;
  time_stream.imbue(locale);
  time_stream << time;
  const std::string time_string = time_stream.str();
  mvwaddnstr(stdscr, row-1,0, time_string.c_str(), time_string.size());
  waddch(stdscr, ' ');
  waddnstr(stdscr, machine.c_str(), machine.size());
  waddch(stdscr, ' ');
  waddnstr(stdscr, process.c_str(), process.size());
  waddstr(stdscr, ": ");
  const size_t message_length = std::min(message.size(), col - time_string.size() - machine.size() - process.size() - 4 - 1);
  waddnstr(stdscr, message.c_str(), message_length);
  waddch(stdscr, '\n');

  if (color != 0)
  {
    attroff(COLOR_PAIR(color));
  }

  std::for_each(in_stream_error_lines.begin(), in_stream_error_lines.end(), [](int& value) { --value; });
  assert(std::all_of(in_stream_error_lines.begin(), in_stream_error_lines.end(), [all_above_errors_line = this->all_above_errors_line](const int& value) {
        return value >= all_above_errors_line;
  }));


  switch (level)
  {
    case Level::emerg:
    case Level::crit:
    case Level::err:
      {
        assert(row-2 >= all_above_errors_line);
        assert(row-2 <= this->row);
        in_stream_error_lines.push_back(row-2);
        auto timer = std::make_unique<boost::asio::deadline_timer>(io_service);
        timer->expires_from_now(error_visibility_duration);
        timer->async_wait(std::bind(&outputter::errorTimeout, this));
        timers.push_back(std::move(timer));
      }
      break;
    default:
      break;
  }

  if (errors_scroll_out > 0)
  {
    assert(all_above_errors_line > 0);
    --all_above_errors_line;
    assert(std::all_of(in_stream_error_lines.begin(), in_stream_error_lines.end(), [all_above_errors_line = this->all_above_errors_line](const int& value) {
          return value <= all_above_errors_line;
    }));
    assert(errors_scroll_out > 0);
    --errors_scroll_out;
  }

  while (!in_stream_error_lines.empty() && in_stream_error_lines.front() == all_above_errors_line)
  {
    ++all_above_errors_line;
    in_stream_error_lines.erase(in_stream_error_lines.begin());

    assert(std::all_of(in_stream_error_lines.begin(), in_stream_error_lines.end(), [all_above_errors_line = this->all_above_errors_line](const int& value) {
          return value >= all_above_errors_line;
    }));
  }

  assert(std::all_of(in_stream_error_lines.begin(), in_stream_error_lines.end(), [all_above_errors_line = this->all_above_errors_line](const int& value) {
        return value >= all_above_errors_line;
  }));

  refresh();

  if (errors_scroll_out == 0)
  {
    wsetscrreg(stdscr, all_above_errors_line, row-1);
  }
}

void outputter::errorTimeout()
{
  if (all_above_errors_line > 0)
  {
    ++errors_scroll_out;
  }
  else
  {
    in_stream_error_lines.erase(in_stream_error_lines.begin());
  }

  timers.erase(timers.begin());
}
