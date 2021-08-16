#ifndef DOWNLOAD_HPP
#define DOWNLOAD_HPP

/**
 * \file
 * \brief Asynchronous network transfers.
 */

#include <istream>
#include <list>
#include <memory>
#include <string>

#include "Signal.hpp"

/// Used to launch downloads on another thread and observe the result.
class Download
{
  /// Passed to CURL; supports multiple protocols.
  std::string mResourceLink;
  /// Used to disconnect from the worker prematurely.
  std::list<Connection> mConnectionList;
  /// Store the error message after the download finishes.
  std::string mErrorMessage;
  /// Store the temporary file after the download finishes.
  std::unique_ptr<std::istream> mInputStream;

public:
  Signal<> Failed;
  Signal<> Finished;

  Download() = default;
  explicit Download(std::string aResourceLink);
  Download(const Download& aOther) = delete;
  Download& operator=(const Download& aOther) = delete;
  ~Download();

  const std::string& GetResourceLink() const;
  std::string& GetResourceLink();

  const std::string& GetErrorMessage() const;
  const std::unique_ptr<std::istream>& GetInputStream() const;
  std::unique_ptr<std::istream>& GetInputStream();

  /// Enqueue the download on another thread.
  void Launch();
};

void
ForkDownloadThread();
void
JoinDownloadThread();

#endif
