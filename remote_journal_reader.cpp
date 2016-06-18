#include <systemd/sd-journal.h>

#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/endian/conversion.hpp>

#include "remote_journal_reader.h"

remote_journal_reader::remote_journal_reader(boost::asio::io_service& io_service, const std::string& path, const std::string& address, outputter& out)
  : resolver(io_service)
  , socket(io_service)
  , out(out)
  , query(address, "19531")
{
  const std::string file_path = path + "/" + address;
  file.open(file_path, std::fstream::in | std::fstream::out);
  if (file.fail())
  {
    file.open(file_path, std::fstream::in | std::fstream::out | std::fstream::trunc);
  }

  file >> cursor;

  start();
}

void remote_journal_reader::start()
{
  resolver.async_resolve(query, boost::bind(&remote_journal_reader::resolved, this, _1, _2));
}

void remote_journal_reader::resolved(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator iterator)
{
  if (ec)
  {
    sd_journal_print(LOG_ERR, "Error resolving address '%s': %s", query.host_name().c_str(), ec.message().c_str());
    return;
  }

  if (iterator == boost::asio::ip::tcp::resolver::iterator())
  {
    sd_journal_print(LOG_ERR, "Resolving host '%s' returned no results.", query.host_name().c_str());
    return;
  }

  endpoint = *iterator;
  connect(++iterator);
}

void remote_journal_reader::connect(boost::asio::ip::tcp::resolver::iterator next_iterator)
{
  socket.async_connect(endpoint, boost::bind(&remote_journal_reader::handle_connect, this, _1, next_iterator));
}

void remote_journal_reader::handle_connect(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator next_iterator)
{
  if (ec)
  {
    sd_journal_print(LOG_ERR, "Failed connecting to '%s' with error: %s", query.host_name().c_str(), ec.message().c_str());

    if (next_iterator != boost::asio::ip::tcp::resolver::iterator())
    {
      // Try next
      socket.close();
      endpoint = *next_iterator;
      connect(++next_iterator);
    }
    else
    {
      sd_journal_print(LOG_ERR, "Failed connecting to any address of host '%s'.", query.host_name().c_str());
    }

    return;
  }

  boost::asio::streambuf request;
  std::ostream request_stream(&request);
  request_stream << "GET /entries?boot&follow HTTP/1.0\r\n";
  request_stream << "Accept: application/vnd.fdo.journal\r\n";
  if (!cursor.empty())
  {
    request_stream << "Range: entries=" << cursor << "\r\n";
  }
  request_stream << "\r\n";
  boost::asio::async_write(socket, request, boost::bind(&remote_journal_reader::handle_write_request, this, _1));
}

void remote_journal_reader::handle_write_request(const boost::system::error_code& ec)
{
  if (ec)
  {
    std::stringstream remote_endpoint;
    remote_endpoint << socket.remote_endpoint();
    sd_journal_print(LOG_ERR, "Failed writing to '%s' with error: %s", remote_endpoint.str().c_str(), ec.message().c_str());

    socket.close();
    start();
    return;
  }

  boost::asio::async_read_until(socket, response, "\r\n\r\n", boost::bind(&remote_journal_reader::handle_read_header, this, _1));
}

void remote_journal_reader::handle_read_header(const boost::system::error_code& ec)
{
  if (ec)
  {
    std::stringstream remote_endpoint;
    remote_endpoint << socket.remote_endpoint();
    sd_journal_print(LOG_ERR, "Failed reading from '%s' with error: %s", remote_endpoint.str().c_str(), ec.message().c_str());

    socket.close();
    start();
    return;
  }

  response.consume(response.size());

  boost::asio::async_read_until(socket, response, '\n', boost::bind(&remote_journal_reader::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void remote_journal_reader::update_cursor(const std::string& cursor)
{
  this->cursor = cursor;
  file.clear();
  file.seekp(0);
  file << cursor;
  file.flush();
}

void remote_journal_reader::handle_read(const boost::system::error_code& ec, size_t bytes_transferred)
{
  if (ec)
  {
    std::stringstream remote_endpoint;
    remote_endpoint << socket.remote_endpoint();
    sd_journal_print(LOG_ERR, "Failed reading from '%s' with error: %s", remote_endpoint.str().c_str(), ec.message().c_str());
    socket.close();
    start();
    return;
  }

  std::string line(boost::asio::buffers_begin(response.data()), boost::asio::buffers_begin(response.data()) + bytes_transferred);

  if (line == "\n")
  {
    response.consume(bytes_transferred);

    {
      auto it = values.find("__CURSOR");
      if (it != values.end())
      {
        update_cursor(it->second);
      }
    }

    out.add_line(values);

    values.clear();

    boost::asio::async_read_until(socket, response, '\n', boost::bind(&remote_journal_reader::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
    return;
  }

  size_t pos = line.find('=');

  if (pos == std::string::npos)
  {
    // No equal sign, this field is serialized in a binary safe way.

    binary_field_name = line.substr(0, line.size() - 1);

    response.consume(bytes_transferred);

    boost::asio::async_read(socket, response, boost::asio::transfer_exactly(8), boost::bind(&remote_journal_reader::handle_read_binary_length, this, _1));
  }
  else
  {
    // Normal field.
    values[line.substr(0, pos)] = line.substr(pos+1, line.size()-pos-1-1);

    response.consume(bytes_transferred);

    boost::asio::async_read_until(socket, response, '\n', boost::bind(&remote_journal_reader::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
  }
}

void remote_journal_reader::handle_read_binary_length(const boost::system::error_code& ec)
{
  if (ec)
  {
    std::stringstream remote_endpoint;
    remote_endpoint << socket.remote_endpoint();
    sd_journal_print(LOG_ERR, "Failed reading length of binary journal entry field from '%s' with error: %s", remote_endpoint.str().c_str(), ec.message().c_str());
    socket.close();
    start();
    return;
  }

  const uint64_t* pointer = boost::asio::buffer_cast<const uint64_t*>(response.data());
  uint64_t size = *pointer;
  boost::endian::little_to_native_inplace(size);

  response.consume(8);

  boost::asio::async_read(socket, response, boost::asio::transfer_exactly(size), boost::bind(&remote_journal_reader::handle_read_binary, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void remote_journal_reader::handle_read_binary(const boost::system::error_code& ec, size_t bytes_transferred)
{
  if (ec)
  {
    std::stringstream remote_endpoint;
    remote_endpoint << socket.remote_endpoint();
    sd_journal_print(LOG_ERR, "Failed reading binary journal entry field from '%s' with error: %s", remote_endpoint.str().c_str(), ec.message().c_str());
    socket.close();
    start();
    return;
  }

  // Ignore binary fields.
  response.consume(bytes_transferred);

  boost::asio::async_read_until(socket, response, '\n', boost::bind(&remote_journal_reader::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}


