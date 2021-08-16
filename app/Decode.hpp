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
  Signal<> Failed;
  Signal<> Finished;

  ApiQuery() = default;
  explicit ApiQuery(std::string aResourceLink);
  explicit ApiQuery(std::unique_ptr<std::istream> aInput);
  ApiQuery(const ApiQuery& aOther) = delete;
  ApiQuery& operator=(const ApiQuery& aOther) = delete;
  ~ApiQuery();

  const std::string& GetResourceLink() const;
  std::string& GetResourceLink();

  const std::unique_ptr<std::istream>& GetInputStream() const;
  std::unique_ptr<std::istream>& GetInputStream();

  const std::string& GetErrorMessage() const;
  const decltype(mResult)& GetResult() const;
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
  Signal<> Failed;
  Signal<> Finished;

  Image() = default;
  explicit Image(std::string aResourceLink);
  explicit Image(std::unique_ptr<std::istream> aInput);
  Image(const Image& aOther) = delete;
  Image& operator=(const Image& aOther) = delete;
  ~Image();

  const std::string& GetResourceLink() const;
  std::string& GetResourceLink();

  const std::unique_ptr<std::istream>& GetInputStream() const;
  std::unique_ptr<std::istream>& GetInputStream();

  const std::string& GetErrorMessage() const;
  const decltype(mSurface)& GetSurface() const;
  decltype(mSurface)& GetSurface();

  /// Enqueue the operation on another thread.
  void Launch();

private:
  /// Called when downloads are finished successfully.
  void OnDownloadFinished(std::unique_ptr<std::istream> aInput);
};

void
ForkDecodeThread();
void
JoinDecodeThread();

#endif
