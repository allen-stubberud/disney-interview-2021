#ifndef IMAGE_HPP
#define IMAGE_HPP

/**
 * \file
 * \brief Asynchronous i/o computations.
 */

#include <istream>
#include <list>
#include <memory>
#include <string>
#include <variant>

#include <SDL.h>

#include "Download.hpp"
#include "Json.hpp"
#include "Signal.hpp"

/// Used to decode the web API on another thread.
class ApiQuery
{
  /// Location from which the image is loaded.
  std::variant<Download, std::unique_ptr<std::istream>> mDataSource;
  /// Used to disconnect from the worker prematurely.
  std::list<Connection> mConnectionList;
  /// Store the error message after the decode finishes.
  std::string mErrorMessage;
  /// Store the result of the JSON parsing operation.
  std::variant<ApiHome, ApiFuzzySet> mResult;

public:
  /// Emitted when the download or parser fails.
  Signal<> Failed;
  /// Emitted when the parsing is finished.
  Signal<> Finished;

  /// Initialize without any inputs.
  ApiQuery() = default;
  /// Initialize with the given resource link.
  explicit ApiQuery(std::string aResourceLink);
  /// Initialize with the given file data.
  explicit ApiQuery(std::unique_ptr<std::istream> aInput);

  ApiQuery(const ApiQuery& aOther) = delete;
  ApiQuery& operator=(const ApiQuery& aOther) = delete;

  /// Cancel the operation; observers will no longer be notified.
  ~ApiQuery();

  /// Do not use this after the download is launched.
  const std::string& GetResourceLink() const;
  /// Do not use this after the download is launched.
  std::string& GetResourceLink();

  /// Do not use this after the download is launched.
  const std::unique_ptr<std::istream>& GetInputStream() const;
  /// Do not use this after the download is launched.
  std::unique_ptr<std::istream>& GetInputStream();

  /// Error string from the downloader or parser.
  const std::string& GetErrorMessage() const;
  /// Buffer containing the full API response.
  const decltype(mResult)& GetResult() const;
  /// Buffer containing the full API response.
  decltype(mResult)& GetResult();

  /// Enqueue the operation on another thread.
  void Launch();

private:
  /// Called when downloads are finished successfully.
  void OnDownloadFinished(std::unique_ptr<std::istream> aInput);
};

namespace detail::Decode {

struct ImageDeleter
{
  void operator()(SDL_Surface* s) const { SDL_FreeSurface(s); }
};

}

/// Used to decode images on another thread.
class Image
{
  /// Location from which the image is loaded.
  std::variant<Download, std::unique_ptr<std::istream>> mDataSource;
  /// Used to disconnect from the worker prematurely.
  std::list<Connection> mConnectionList;
  /// Store the error message after the decode finishes.
  std::string mErrorMessage;
  /// Store the pixel buffer after the decode finishes.
  std::unique_ptr<SDL_Surface, detail::Decode::ImageDeleter> mSurface;

public:
  /// Emitted when the download or decoder fails.
  Signal<> Failed;
  /// Emitted when the image decoder is finished.
  Signal<> Finished;

  /// Initialize without any inputs.
  Image() = default;
  /// Initialize with the given resource link.
  explicit Image(std::string aResourceLink);
  /// Initialize with the given file data.
  explicit Image(std::unique_ptr<std::istream> aInput);

  Image(const Image& aOther) = delete;
  Image& operator=(const Image& aOther) = delete;

  /// Cancel the operation; observers will no longer be notified.
  ~Image();

  /// Do not use this after the download is launched.
  const std::string& GetResourceLink() const;
  /// Do not use this after the download is launched.
  std::string& GetResourceLink();

  /// Do not use this after the download is launched.
  const std::unique_ptr<std::istream>& GetInputStream() const;
  /// Do not use this after the download is launched.
  std::unique_ptr<std::istream>& GetInputStream();

  /// Error string from the downloader or decoder.
  const std::string& GetErrorMessage() const;
  /// SDL surface containing metrics and pixel data.
  const decltype(mSurface)& GetSurface() const;
  /// SDL surface containing metrics and pixel data.
  decltype(mSurface)& GetSurface();

  /// Enqueue the operation on another thread.
  void Launch();

private:
  /// Called when downloads are finished successfully.
  void OnDownloadFinished(std::unique_ptr<std::istream> aInput);
};

#endif
