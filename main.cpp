#include <boost/asio.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <string>

#include "ncurses.h"
#include "remote_journal_reader.h"
#include "local_journal_reader.h"
#include "outputter.h"

int main(int argc, char** argv)
{
  std::string cursor_path(".");
  std::vector<std::string> remote_hosts;
  bool use_local_journal = false;

  {
    namespace po = boost::program_options;
    po::options_description description("options");

    description.add_options()
      ("help,h", "print this help message")
      ("local,l", "read from the local systemd journal")
      ("cursor-path,c", po::value<std::string>()->value_name("path")->default_value(cursor_path), "path where the current read position for each remote host is stored");

    po::options_description hidden("Hidden options");
    hidden.add(description);
    hidden.add_options()
      ("remote-hosts", po::value<std::vector<std::string>>(), "remote hosts to connect to");

    po::positional_options_description positionals;
    positionals.add("remote-hosts", -1);

    po::variables_map vm;

    try
    {
      po::store(po::command_line_parser(argc, argv).options(hidden).positional(positionals).run(), vm);
    } catch (const po::unknown_option& e)
    {
      std::cerr << e.what() << '\n';
      return 1;
    }

    po::notify(vm);

    if (vm.count("help"))
    {
      std::cout << "journal_aggregator [options] remote-hosts...\n";
      std::cout << description << '\n';
      std::cout << "remote-hosts are hostnames or IPs of other machines were journal-gatewayd is running.\n";
      return 1;
    }

    if (vm.count("remote-hosts") == 0 && vm.count("local") == 0)
    {
      std::cout << "At least one remote host or the local journal must be specified.\n";
      return 1;
    }

    cursor_path = vm["cursor-path"].as<std::string>();

    if (vm.count("remote-hosts") > 0)
    {
      remote_hosts = vm["remote-hosts"].as<std::vector<std::string>>();
    }
    use_local_journal = vm.count("local") > 0;
  }

  ncurses n;
  boost::asio::io_service io_service(1);
  boost::asio::io_service::work work(io_service);

  outputter out(io_service);

  try
  {
    std::unique_ptr<local_journal_reader> local_reader;
    if (use_local_journal)
    {
      local_reader = std::make_unique<local_journal_reader>(io_service, cursor_path, out);
    }

    std::vector<std::unique_ptr<remote_journal_reader>> readers;
    readers.reserve(remote_hosts.size());
    for (auto&& remote_host : remote_hosts)
    {
      readers.push_back(std::make_unique<remote_journal_reader>(io_service, cursor_path, remote_host, out));
    }

    io_service.run();
  } catch (const local_journal_reader::call_error& e)
  {
    std::cerr << "Fatal error with local journal: " << e.what() << '\n';
  }

  return 0;
}
