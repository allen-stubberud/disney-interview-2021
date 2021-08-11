#ifndef DOWNLOAD_HPP
#define DOWNLOAD_HPP

#include <istream>
#include <list>
#include <memory>
#include <string>

#include "Signal.hpp"

class Download
{
  std::string mResourceLink;
  std::list<Connection> mConnectionList;

public:
  static void ForkThread();
  static void JoinThread();

  Signal<const std::string&> Failed;
  Signal<std::unique_ptr<std::istream>&> Finished;

  Download() = default;
  explicit Download(std::string aResourceLink);
  ~Download();

  const std::string& GetResourceLink() const;
  std::string& GetResourceLink();

  void Launch();
};

#endif
