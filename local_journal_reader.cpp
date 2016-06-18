#include "local_journal_reader.h"

#include <iostream>

#include <boost/format.hpp>
#include <boost/bind.hpp>

local_journal_reader::call_error::call_error(const std::string& function, int error_code)
  : runtime_error((boost::format("%1% returned error: %2%") % function % strerror(error_code)).str())
{
}

local_journal_reader::local_journal_reader(boost::asio::io_service& io_service, const std::string& cursor_path, outputter& out)
  : out(out)
{
  const std::string file_path = cursor_path + "/local";
  file.open(file_path, std::fstream::in | std::fstream::out);
  if (file.fail())
  {
    file.open(file_path, std::fstream::in | std::fstream::out | std::fstream::trunc);
  }

  file >> cursor;

  int error_code = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);

  if (error_code != 0)
  {
    throw call_error("sd_journal_open", error_code);
  }

  int fd = sd_journal_get_fd(journal);

  if (fd < 0)
  {
    throw call_error("sd_journal_get_fd", error_code);
  }

  if (!cursor.empty())
  {
    error_code = sd_journal_seek_cursor(journal, cursor.c_str());

    if (error_code != 0)
    {
      sd_journal_print(LOG_WARNING, "Error calling seeking to last cursor: %s", strerror(error_code));
    }
  }

  journal_descriptor = std::make_unique<boost::asio::posix::stream_descriptor>(io_service, fd);

  read_journal();
  async_read();
}

local_journal_reader::~local_journal_reader()
{
  sd_journal_close(journal);
}

void local_journal_reader::update_cursor(const std::string& cursor)
{
  this->cursor = cursor;
  file.clear();
  file.seekp(0);
  file << cursor;
  file.flush();
}

void local_journal_reader::read_journal()
{
  int error_code;

  while ((error_code = sd_journal_next(journal)) == 1)
  {
    std::map<std::string, std::string> values;
    const void* data;
    size_t length;
    SD_JOURNAL_FOREACH_DATA(journal, data, length) {
      std::string line(static_cast<const char*>(data), length);
      auto equal_pos = line.find('=');
      if (equal_pos == std::string::npos)
        continue;

      values[line.substr(0, equal_pos)] = line.substr(equal_pos+1);
    }

    uint64_t usec;
    sd_journal_get_realtime_usec(journal, &usec);
    values["__REALTIME_TIMESTAMP"] = std::to_string(usec);

    {
      char* cursor;
      sd_journal_get_cursor(journal, &cursor);
      update_cursor(cursor);
      free(cursor);
    }

    out.add_line(values);
  }

  if (error_code < 0)
  {
    sd_journal_print(LOG_WARNING, "Error calling sd_journal_next: %s", strerror(error_code));
  }
}

void local_journal_reader::on_data_available(boost::system::error_code ec)
{
  if (ec)
  {
      sd_journal_print(LOG_ERR, "Error polling journal file descriptor: %s", ec.message().c_str());
  }
  else
  {

    int error_code = sd_journal_process(journal);
    switch (error_code)
    {
      case SD_JOURNAL_NOP:
        break;
      case SD_JOURNAL_APPEND:
        read_journal();
        break;
      case SD_JOURNAL_INVALIDATE:
        //TODO: How to handle?
        break;
      default:
        sd_journal_print(LOG_WARNING, "Unexpected return code from sd_journal_process: %i", error_code);
    }
  }

  async_read();
}

void local_journal_reader::async_read()
{
  journal_descriptor->async_read_some(boost::asio::null_buffers(),
      boost::bind(&local_journal_reader::on_data_available, this, _1));
}
