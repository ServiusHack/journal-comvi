#pragma once

#include <fstream>

#include <boost/asio.hpp>

#include "outputter.h"

class remote_journal_reader
{
  boost::asio::ip::tcp::resolver resolver;
  boost::asio::ip::tcp::socket socket;
  boost::asio::streambuf response;
  outputter& out;
  boost::asio::ip::tcp::resolver::query query;
  boost::asio::ip::tcp::endpoint endpoint;
  std::map<std::string, std::string> values;
  std::string binary_field_name;
  std::fstream file;
  std::string cursor;

  void start();

  void resolved(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator iterator);

  void connect(boost::asio::ip::tcp::resolver::iterator next_iterator);

  void handle_connect(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator next_iterator);

  void handle_write_request(const boost::system::error_code& ec);

  void handle_read_header(const boost::system::error_code& ec);

  void update_cursor(const std::string& cursor);

  void handle_read(const boost::system::error_code& ec, size_t bytes_transferred);

  void handle_read_binary_length(const boost::system::error_code& ec);

  void handle_read_binary(const boost::system::error_code& ec, size_t bytes_transferred);

public:
  remote_journal_reader(boost::asio::io_service& io_service, const std::string& path, const std::string& address, outputter& out);
};
