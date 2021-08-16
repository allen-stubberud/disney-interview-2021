#include "Decode.hpp"

#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#include <SDL_image.h>

#include "Main.hpp"

namespace {

struct ApiTask
{
  std::unique_ptr<std::istream> Stream;
  Signal<std::string> Failed;
  Signal<std::variant<ApiHome, ApiFuzzySet>> Finished;
};

struct ImageTask
{
  std::unique_ptr<std::istream> Stream;
  Signal<std::string> Failed;
  Signal<std::unique_ptr<SDL_Surface, detail::Decode::ImageDeleter>> Finished;
};

class DecodeThread
{
  /// Whether the main loop for the thread should exit.
  bool mRunning;
  /// All downloads and i/o will happen on this thread.
  std::thread mThread;
  /// Synchronize access to the transfer queue.
  std::mutex mMutex;
  /// Allow the thread to wait for more inputs.
  std::condition_variable mCondition;

  using ApiTaskRef = std::unique_ptr<ApiTask>;
  using ImageTaskRef = std::unique_ptr<ImageTask>;

  /// Sequence of image files to be decoded.
  std::queue<std::variant<ApiTaskRef, ImageTaskRef>> mQueue;

public:
  DecodeThread()
    : mRunning(true)
  {
    mThread = std::thread(std::bind(&DecodeThread::MainLoop, this));
  }

  DecodeThread(const DecodeThread& aOther) = delete;
  DecodeThread& operator=(const DecodeThread& aOther) = delete;

  ~DecodeThread()
  {
    mRunning = false;
    mCondition.notify_all();
    mThread.join();
  }

  void Enqueue(decltype(mQueue)::value_type aTask)
  {
    std::unique_lock<std::mutex> lock(mMutex);
    mQueue.emplace(std::move(aTask));
    mCondition.notify_all();
  }

private:
  static size_t ReadProc(SDL_RWops* aStream,
                         void* aBuffer,
                         size_t aDimA,
                         size_t aDimB)
  {
    auto ptr = aStream->hidden.unknown.data1;
    auto stream = reinterpret_cast<std::istream*>(ptr);

    // This should automatically pick up error conditions.
    stream->read(reinterpret_cast<char*>(aBuffer), aDimA * aDimB);
    return stream->gcount() / aDimA;
  }

  static Sint64 SeekProc(SDL_RWops* aStream, Sint64 aPos, int aWhence)
  {
    auto ptr = aStream->hidden.unknown.data1;
    auto stream = reinterpret_cast<std::istream*>(ptr);
    std::ios_base::seekdir dir = std::ios_base::beg;

    switch (aWhence) {
      case RW_SEEK_SET:
        dir = std::ios_base::beg;
        break;
      case RW_SEEK_CUR:
        dir = std::ios_base::cur;
        break;
      case RW_SEEK_END:
        dir = std::ios_base::end;
        break;
    }

    stream->seekg(aPos, dir);
    return aPos;
  }

  static Sint64 SizeProc(SDL_RWops* aStream)
  {
    auto ptr = aStream->hidden.unknown.data1;
    auto stream = reinterpret_cast<std::istream*>(ptr);

    std::streamoff save = stream->tellg();
    stream->seekg(0, std::ios_base::end);
    std::streamoff size = stream->tellg();
    stream->seekg(save);
    return size;
  }

  void MainLoop()
  {
    while (mRunning) {
      std::optional<decltype(mQueue)::value_type> job;

      // Grab the job from the top of the queue.
      // Hold the lock for the minimum amount of time.
      {
        std::unique_lock<std::mutex> lock(mMutex);
        if (mQueue.empty()) {
          mCondition.wait(lock);
        } else {
          job.emplace(std::move(mQueue.front()));
          mQueue.pop();
        }
      }

      // Process the job without holding the lock.
      if (job)
        std::visit([&](auto&& obj) { Process(std::move(obj)); }, job.value());
    }
  }

  void Process(std::unique_ptr<ApiTask> aTask)
  {
    std::variant<ApiHome, ApiFuzzySet> result;
    std::optional<std::string> error;

    try {
      result = ReadApi(*aTask->Stream);
    } catch (std::exception& ex) {
      error.emplace(ex.what());
    }

    ApiTask* signal = aTask.release();
    signal->Stream.reset();

    if (error) {
      InvokeAsync([error, signal]() {
        signal->Failed.Notify(error.value());
        delete signal;
      });
    } else {
      InvokeAsync([result = std::move(result), signal]() {
        signal->Finished.Notify(std::move(result));
        delete signal;
      });
    }
  }

  void Process(std::unique_ptr<ImageTask> aTask)
  {
    ImageTask* signal = aTask.release();
    SDL_Surface* result;

    {
      SDL_RWops ops;
      memset(&ops, 0, sizeof(ops));
      ops.read = ReadProc;
      ops.seek = SeekProc;
      ops.size = SizeProc;
      ops.hidden.unknown.data1 = signal->Stream.get();
      result = IMG_Load_RW(&ops, 0);
    }

    signal->Stream.reset();

    if (result) {
      InvokeAsync([result, signal]() {
        std::unique_ptr<SDL_Surface, detail::Decode::ImageDeleter> ptr(result);
        signal->Finished.Notify(std::move(ptr));
        delete signal;
      });
    } else {
      std::string message = IMG_GetError();
      InvokeAsync([message, signal]() {
        signal->Failed.Notify(message);
        delete signal;
      });
    }
  }
};

DecodeThread* gThread = nullptr;

}

void
ForkDecodeThread()
{
  assert(!gThread);
  gThread = new DecodeThread;
}

void
JoinDecodeThread()
{
  assert(gThread);
  delete gThread;
  gThread = nullptr;
  IMG_Quit();
}

ApiQuery::ApiQuery(std::string aResourceLink)
  : mDataSource(std::in_place_index<0>, std::move(aResourceLink))
{}

ApiQuery::ApiQuery(std::unique_ptr<std::istream> aInput)
  : mDataSource(std::in_place_index<1>, std::move(aInput))
{}

ApiQuery::~ApiQuery()
{
  for (auto& elem : mConnectionList)
    elem.Disconnect();
}

const std::string&
ApiQuery::GetResourceLink() const
{
  // This one throws exceptions.
  return std::get<0>(mDataSource).GetResourceLink();
}

std::string&
ApiQuery::GetResourceLink()
{
  if (mDataSource.index() != 0)
    mDataSource.emplace<0>();

  return std::get<0>(mDataSource).GetResourceLink();
}

const std::unique_ptr<std::istream>&
ApiQuery::GetInputStream() const
{
  // This one throws exceptions.
  return std::get<1>(mDataSource);
}

std::unique_ptr<std::istream>&
ApiQuery::GetInputStream()
{
  if (mDataSource.index() != 1)
    mDataSource.emplace<1>();

  return std::get<1>(mDataSource);
}

const std::string&
ApiQuery::GetErrorMessage() const
{
  return mErrorMessage;
}

const decltype(ApiQuery::mResult)&
ApiQuery::GetResult() const
{
  return mResult;
}

decltype(ApiQuery::mResult)&
ApiQuery::GetResult()
{
  return mResult;
}

void
ApiQuery::Launch()
{
  if (mDataSource.index() == 0) {
    auto& download = std::get<Download>(mDataSource);
    auto it1 = mConnectionList.emplace(mConnectionList.end());
    auto it2 = mConnectionList.emplace(mConnectionList.end());

    *it1 = download.Failed.Connect(
      // Note: disconnection deletes the lambda itself.
      [&, it1, it2]() {
        mErrorMessage = download.GetErrorMessage();
        Failed.Notify();

        auto self = this;
        auto copy1 = it1;
        auto copy2 = it2;
        copy1->Disconnect();
        copy2->Disconnect();
        self->mConnectionList.erase(copy1);
        self->mConnectionList.erase(copy2);
      });

    *it2 = download.Finished.Connect(
      // Note: disconnection deletes the lambda itself.
      [&, it1, it2]() {
        OnDownloadFinished(std::move(download.GetInputStream()));

        auto self = this;
        auto copy1 = it1;
        auto copy2 = it2;
        copy1->Disconnect();
        copy2->Disconnect();
        self->mConnectionList.erase(copy1);
        self->mConnectionList.erase(copy2);
      });

    download.Launch();
  } else if (mDataSource.index() == 1) {
    OnDownloadFinished(std::move(std::get<1>(mDataSource)));
  }
}

void
ApiQuery::OnDownloadFinished(std::unique_ptr<std::istream> aInput)
{
  auto task = std::make_unique<ApiTask>();
  auto it1 = mConnectionList.emplace(mConnectionList.end());
  auto it2 = mConnectionList.emplace(mConnectionList.end());
  task->Stream = std::move(aInput);

  *it1 = task->Failed.Connect(
    // The main loop will delete the signal.
    [this, it1, it2](std::string aMessage) {
      mConnectionList.erase(it1);
      mConnectionList.erase(it2);
      mErrorMessage = std::move(aMessage);
      Failed.Notify();
    });

  *it2 = task->Finished.Connect(
    // The main loop will delete the signal.
    [this, it1, it2](decltype(mResult) aImage) {
      mConnectionList.erase(it1);
      mConnectionList.erase(it2);
      mResult = std::move(aImage);
      Finished.Notify();
    });

  gThread->Enqueue(std::move(task));
}

Image::Image(std::string aResourceLink)
  : mDataSource(std::in_place_index<0>, std::move(aResourceLink))
{}

Image::Image(std::unique_ptr<std::istream> aInput)
  : mDataSource(std::in_place_index<1>, std::move(aInput))
{}

Image::~Image()
{
  for (auto& elem : mConnectionList)
    elem.Disconnect();
}

const std::string&
Image::GetResourceLink() const
{
  // This one throws exceptions.
  return std::get<0>(mDataSource).GetResourceLink();
}

std::string&
Image::GetResourceLink()
{
  if (mDataSource.index() != 0)
    mDataSource.emplace<0>();

  return std::get<0>(mDataSource).GetResourceLink();
}

const std::unique_ptr<std::istream>&
Image::GetInputStream() const
{
  // This one throws exceptions.
  return std::get<1>(mDataSource);
}

std::unique_ptr<std::istream>&
Image::GetInputStream()
{
  if (mDataSource.index() != 1)
    mDataSource.emplace<1>();

  return std::get<1>(mDataSource);
}

const std::string&
Image::GetErrorMessage() const
{
  return mErrorMessage;
}

const decltype(Image::mSurface)&
Image::GetSurface() const
{
  return mSurface;
}

decltype(Image::mSurface)&
Image::GetSurface()
{
  return mSurface;
}

void
Image::Launch()
{
  if (mDataSource.index() == 0) {
    auto& download = std::get<Download>(mDataSource);
    auto it1 = mConnectionList.emplace(mConnectionList.end());
    auto it2 = mConnectionList.emplace(mConnectionList.end());

    *it1 = download.Failed.Connect(
      // Note: disconnection deletes the lambda itself.
      [&, it1, it2]() {
        mErrorMessage = download.GetErrorMessage();
        Failed.Notify();

        auto self = this;
        auto copy1 = it1;
        auto copy2 = it2;
        copy1->Disconnect();
        copy2->Disconnect();
        self->mConnectionList.erase(copy1);
        self->mConnectionList.erase(copy2);
      });

    *it2 = download.Finished.Connect(
      // Note: disconnection deletes the lambda itself.
      [&, it1, it2]() {
        OnDownloadFinished(std::move(download.GetInputStream()));

        auto self = this;
        auto copy1 = it1;
        auto copy2 = it2;
        copy1->Disconnect();
        copy2->Disconnect();
        self->mConnectionList.erase(copy1);
        self->mConnectionList.erase(copy2);
      });

    download.Launch();
  } else if (mDataSource.index() == 1) {
    OnDownloadFinished(std::move(std::get<1>(mDataSource)));
  }
}

void
Image::OnDownloadFinished(std::unique_ptr<std::istream> aInput)
{
  auto task = std::make_unique<ImageTask>();
  auto it1 = mConnectionList.emplace(mConnectionList.end());
  auto it2 = mConnectionList.emplace(mConnectionList.end());
  task->Stream = std::move(aInput);

  *it1 = task->Failed.Connect(
    // The main loop will delete the signal.
    [this, it1, it2](std::string aMessage) {
      mConnectionList.erase(it1);
      mConnectionList.erase(it2);
      mErrorMessage = std::move(aMessage);
      Failed.Notify();
    });

  *it2 = task->Finished.Connect(
    // The main loop will delete the signal.
    [this, it1, it2](decltype(mSurface) aImage) {
      mConnectionList.erase(it1);
      mConnectionList.erase(it2);
      mSurface = std::move(aImage);
      Finished.Notify();
    });

  gThread->Enqueue(std::move(task));
}
