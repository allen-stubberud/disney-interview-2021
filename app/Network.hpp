#ifndef NETWORK_HPP
#define NETWORK_HPP

/**
 * \file
 * \brief Asynchronous network transfers.
 */

#include <istream>
#include <memory>
#include <string>

#include <sigc++/sigc++.h>

class AsyncDownload
{
  class Private;
  std::unique_ptr<Private> mPrivate;

public:
  mutable sigc::signal<void(std::string)> Failed;
  mutable sigc::signal<void(std::shared_ptr<std::istream>)> Finished;

  AsyncDownload();
  explicit AsyncDownload(std::string aLink);

  AsyncDownload(const AsyncDownload& aOther) = delete;
  AsyncDownload& operator=(const AsyncDownload& aOther) = delete;
  ~AsyncDownload();

  const std::string& GetErrorMessage() const;
  std::shared_ptr<std::istream> GetResult() const;

  void Enqueue();
  void SetLink(std::string aNewValue);
};

/// Initialize CURL library and boot thread.
void
InitNetwork();
/// Shut down thread and clean up CURL library state.
void
FreeNetwork();

#endif
