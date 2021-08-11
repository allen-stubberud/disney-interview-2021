#ifndef IMAGE_HPP
#define IMAGE_HPP

#include <istream>
#include <list>
#include <memory>
#include <string>

#include <SDL.h>

#include "Download.hpp"
#include "Signal.hpp"

class Image
{
  Download mDownload;
  std::list<Connection> mConnectionList;

public:
  static void ForkThread();
  static void JoinThread();

  Signal<const std::string&> Failed;
  Signal<SDL_Surface*&> Finished;

  Image();
  explicit Image(std::string aResourceLink);
  ~Image();

  const std::string& GetResourceLink() const;
  std::string& GetResourceLink();

  void Launch();

private:
  void Launch(std::unique_ptr<std::istream>& aInput);
};

#endif
