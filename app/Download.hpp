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
  /// Emitted when the download encounters an error.
  Signal<> Failed;
  /// Emitted when the download is finished.
  Signal<> Finished;

  /// Initialize with an empty link.
  Download() = default;
  /// Initialize with the given resource link.
  explicit Download(std::string aResourceLink);

  Download(const Download& aOther) = delete;
  Download& operator=(const Download& aOther) = delete;

  /// Cancel the operation; observers will no longer be notified.
  ~Download();

  /// Do not use this after the download is launched.
  const std::string& GetResourceLink() const;
  /// Do not use this after the download is launched.
  std::string& GetResourceLink();

  /// Error string from CURL; available after download fails.
  const std::string& GetErrorMessage() const;
  /// File stream containing full download result.
  const std::unique_ptr<std::istream>& GetInputStream() const;
  /// File stream containing full download result.
  std::unique_ptr<std::istream>& GetInputStream();

  /// Enqueue the download on another thread.
  void Launch();
};

#endif
