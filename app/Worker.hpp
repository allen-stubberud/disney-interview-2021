#ifndef WORKER_HPP
#define WORKER_HPP

/**
 * \file
 * \brief Asynchronous parsing operations.
 */

#include <istream>
#include <memory>
#include <string>
#include <variant>

#include <SDL.h>
#include <sigc++/sigc++.h>

#include "Json.hpp"

class AsyncImage
{
  class Private;
  std::unique_ptr<Private> mPrivate;

public:
  mutable sigc::signal<void(std::string)> Failed;
  mutable sigc::signal<void(std::shared_ptr<SDL_Surface>)> Finished;

  AsyncImage();
  explicit AsyncImage(std::shared_ptr<std::istream> aData);
  explicit AsyncImage(std::string aLink);

  AsyncImage(const AsyncImage& aOther) = delete;
  AsyncImage& operator=(const AsyncImage& aOther) = delete;
  ~AsyncImage();

  const std::string& GetErrorMessage() const;
  std::shared_ptr<SDL_Surface> GetResult() const;

  void Enqueue();
  void SetData(std::shared_ptr<std::istream> aNewValue);
  void SetLink(std::string aNewValue);
};

class AsyncQuery
{
  class Private;
  std::unique_ptr<Private> mPrivate;

public:
  using ResultType = std::variant<ApiHome, ApiFuzzySet>;

  /// Select which construct to parse.
  enum Mode
  {
    Home,
    Dereference,
  };

  mutable sigc::signal<void(std::string)> Failed;
  mutable sigc::signal<void(std::shared_ptr<ResultType>)> Finished;

  AsyncQuery();
  explicit AsyncQuery(std::shared_ptr<std::istream> aData);
  explicit AsyncQuery(std::string aLink);

  AsyncQuery(const AsyncQuery& aOther) = delete;
  AsyncQuery& operator=(const AsyncQuery& aOther) = delete;
  ~AsyncQuery();

  const std::string& GetErrorMessage() const;
  const std::shared_ptr<ResultType> GetResult() const;

  void Enqueue(Mode aMode);
  void SetData(std::shared_ptr<std::istream> aNewValue);
  void SetLink(std::string aNewValue);
};

void
InitWorker();
void
FreeWorker();

#endif
