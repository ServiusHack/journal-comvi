#pragma once

#include <fstream>
#include <string.h>

#include <boost/asio.hpp>

#include <systemd/sd-journal.h>

#include "outputter.h"

class local_journal_reader
{
  sd_journal* journal;
  outputter& out;
  std::fstream file;
  std::string cursor;
  std::unique_ptr<boost::asio::posix::stream_descriptor> journal_descriptor;

  void update_cursor(const std::string& cursor);
  void on_data_available(boost::system::error_code ec);
  void read_journal();
  void async_read();

public:

  class call_error : public std::runtime_error
  {
    public:
    call_error(const std::string& function, int error_code);
  };

  local_journal_reader(boost::asio::io_service& io_service, const std::string& cursor_path, outputter& out);
  ~local_journal_reader();
};
