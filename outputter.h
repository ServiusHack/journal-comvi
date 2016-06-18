#pragma once

#include <boost/asio.hpp>

class outputter
{
  boost::asio::io_service& io_service;
  int row,col;
  int all_above_errors_line = 0;
  std::vector<int> in_stream_error_lines;
  std::vector<std::unique_ptr<boost::asio::deadline_timer>> timers;
  int errors_scroll_out = 0;
  const boost::posix_time::time_duration error_visibility_duration = boost::posix_time::seconds(8);

public:
  outputter(boost::asio::io_service& io_service);

  void add_line(const std::map<std::string, std::string>& values);

  void errorTimeout();
};
