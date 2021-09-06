#include "Worker.hpp"

#include <cassert>
#include <condition_variable>
#include <exception>
#include <list>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <utility>

#include <SDL_image.h>

#include "Helper.hpp"
#include "Main.hpp"
#include "Network.hpp"

//===========================================================================//
//=== Thread ================================================================//
//===========================================================================//

namespace {

class WorkerThread
{
  /// Whether the main loop for the thread should exit.
  bool mRunning;
  /// All downloads and i/o will happen on this thread.
  std::thread mThread;
  /// Synchronize access to the transfer queue.
  std::mutex mMutex;
  /// Allow the thread to wait for more inputs.
  std::condition_variable mCondition;

public:
  struct ImageTask
  {
    std::shared_ptr<std::istream> File;
    sigc::signal<void(std::string)> Failed;
    sigc::signal<void(std::shared_ptr<SDL_Surface>)> Finished;
  };

  struct QueryTask
  {
    AsyncQuery::Mode Mode;
    std::shared_ptr<std::istream> File;
    sigc::signal<void(std::string)> Failed;
    sigc::signal<void(AsyncQuery::ResultType)> Finished;
  };

private:
  using ImageTaskRef = std::unique_ptr<ImageTask>;
  using QueryTaskRef = std::unique_ptr<QueryTask>;

  /// Sequence of image files to be decoded.
  std::queue<std::variant<ImageTaskRef, QueryTaskRef>> mQueue;

public:
  WorkerThread()
    : mRunning(true)
  {
    mThread = std::thread(std::bind(&WorkerThread::MainLoop, this));
  }

  WorkerThread(const WorkerThread& aOther) = delete;
  WorkerThread& operator=(const WorkerThread& aOther) = delete;

  ~WorkerThread()
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

  void Process(std::unique_ptr<ImageTask> aTask)
  {
    SDL_RWops* ops = CppToRW(*aTask->File);
    std::shared_ptr<SDL_Surface> result(IMG_Load_RW(ops, 1), SDL_FreeSurface);

    if (result) {
      InvokeAsync([result, task = aTask.release()]() {
        task->Finished(std::move(result));
        delete task;
      });
    } else {
      std::string message = IMG_GetError();
      InvokeAsync([message = std::move(message), task = aTask.release()]() {
        task->Failed(std::move(message));
        delete task;
      });
    }
  }

  void Process(std::unique_ptr<QueryTask> aTask)
  {
    AsyncQuery::ResultType result;
    std::optional<std::string> error;

    if (aTask->Mode == AsyncQuery::Home)
      try {
        result = ReadApiHome(*aTask->File);
      } catch (std::exception& ex) {
        error.emplace(ex.what());
      }
    else if (aTask->Mode == AsyncQuery::Dereference)
      try {
        result = ReadApiFuzzySet(*aTask->File);
      } catch (std::exception& ex) {
        error.emplace(ex.what());
      }

    if (error)
      InvokeAsync([error = std::move(error), task = aTask.release()] {
        task->Failed(std::move(error.value()));
        delete task;
      });
    else
      InvokeAsync([result = std::move(result), task = aTask.release()] {
        task->Finished(std::move(result));
        delete task;
      });
  }
};

}

//===========================================================================//
//=== Globals ===============================================================//
//===========================================================================//

namespace {

WorkerThread* gThread = nullptr;

}

void
InitWorker()
{
  assert(!gThread);
  gThread = new WorkerThread;
}

void
FreeWorker()
{
  assert(gThread);
  delete gThread;
  gThread = nullptr;
  IMG_Quit();
}

//===========================================================================//
//=== AsyncImage ============================================================//
//===========================================================================//

class AsyncImage::Private
{
  AsyncImage& mParent;
  /// Used to disconnect from signal early when this object dies.
  std::list<sigc::connection> mConnectionList;
  /// Location from which the image is loaded.
  std::variant<AsyncDownload, std::shared_ptr<std::istream>> mDataSource;
  /// Error string if applicable.
  std::optional<std::string> mErrorMessage;
  /// Final data result if applicable.
  std::shared_ptr<SDL_Surface> mResult;

public:
  explicit Private(AsyncImage& aParent)
    : mParent(aParent)
  {}

  Private(AsyncImage& aParent, std::shared_ptr<std::istream> aData)
    : mParent(aParent)
    , mDataSource(std::move(aData))
  {}

  Private(AsyncImage& aParent, std::string aLink)
    : mParent(aParent)
    , mDataSource(std::in_place_type<AsyncDownload>)
  {
    std::get<AsyncDownload>(mDataSource).SetLink(std::move(aLink));
  }

  Private(const Private& aOther) = delete;
  Private& operator=(const Private& aOther) = delete;

  ~Private()
  {
    for (sigc::connection& elem : mConnectionList)
      elem.disconnect();
  }

  const std::string& GetErrorMessage() const { return mErrorMessage.value(); }
  std::shared_ptr<SDL_Surface> GetResult() const { return mResult; }

  void Enqueue()
  {
    if (mDataSource.index() == 0) {
      auto& download = std::get<AsyncDownload>(mDataSource);
      auto it1 = mConnectionList.emplace(mConnectionList.end());
      auto it2 = mConnectionList.emplace(mConnectionList.end());

      *it1 = download.Failed.connect(
        // Strictly normal signal/slot action here.
        [&, it1, it2](std::string aMessage) {
          it1->disconnect();
          it2->disconnect();
          mConnectionList.erase(it1);
          mConnectionList.erase(it2);
          mErrorMessage = std::move(aMessage);
          mParent.Failed(mErrorMessage.value());
        });

      *it2 = download.Finished.connect(
        // Strictly normal signal/slot action here.
        [&, it1, it2](std::shared_ptr<std::istream> aData) {
          it1->disconnect();
          it2->disconnect();
          mConnectionList.erase(it1);
          mConnectionList.erase(it2);
          mDataSource = aData;
          OnDownloadFinished(std::move(aData));
        });

      download.Enqueue();
    } else if (mDataSource.index() == 1) {
      OnDownloadFinished(std::get<1>(mDataSource));
    }
  }

  void SetData(std::shared_ptr<std::istream> aNewValue)
  {
    mDataSource = std::move(aNewValue);
  }

  void SetLink(std::string aNewValue)
  {
    auto& ref = mDataSource.emplace<AsyncDownload>();
    ref.SetLink(std::move(aNewValue));
  }

private:
  void OnDownloadFinished(std::shared_ptr<std::istream> aData)
  {
    auto task = std::make_unique<WorkerThread::ImageTask>();
    auto it1 = mConnectionList.emplace(mConnectionList.end());
    auto it2 = mConnectionList.emplace(mConnectionList.end());
    task->File = std::move(aData);

    *it1 = task->Failed.connect(
      // The main loop will delete the signal.
      [this, it1, it2](std::string aMessage) {
        mConnectionList.erase(it1);
        mConnectionList.erase(it2);
        mErrorMessage = std::move(aMessage);
        mParent.Failed(mErrorMessage.value());
      });

    *it2 = task->Finished.connect(
      // The main loop will delete the signal.
      [this, it1, it2](std::shared_ptr<SDL_Surface> aImage) {
        mConnectionList.erase(it1);
        mConnectionList.erase(it2);
        mResult = std::move(aImage);
        mParent.Finished(mResult);
      });

    gThread->Enqueue(std::move(task));
  }
};

AsyncImage::AsyncImage()
  : mPrivate(new Private(*this))
{}

AsyncImage::AsyncImage(std::shared_ptr<std::istream> aData)
  : mPrivate(new Private(*this, std::move(aData)))
{}

AsyncImage::AsyncImage(std::string aLink)
  : mPrivate(new Private(*this, std::move(aLink)))
{}

AsyncImage::~AsyncImage() = default;

const std::string&
AsyncImage::GetErrorMessage() const
{
  return mPrivate->GetErrorMessage();
}

std::shared_ptr<SDL_Surface>
AsyncImage::GetResult() const
{
  return mPrivate->GetResult();
}

void
AsyncImage::Enqueue()
{
  mPrivate->Enqueue();
}

void
AsyncImage::SetData(std::shared_ptr<std::istream> aNewValue)
{
  mPrivate->SetData(std::move(aNewValue));
}

void
AsyncImage::SetLink(std::string aNewValue)
{
  mPrivate->SetLink(std::move(aNewValue));
}

//===========================================================================//
//=== AsyncQuery ============================================================//
//===========================================================================//

class AsyncQuery::Private
{
  AsyncQuery& mParent;
  /// Used to disconnect from signal early when this object dies.
  std::list<sigc::connection> mConnectionList;
  /// Location from which the image is loaded.
  std::variant<AsyncDownload, std::shared_ptr<std::istream>> mDataSource;
  /// Error string if applicable.
  std::optional<std::string> mErrorMessage;
  /// Final data result if applicable.
  std::shared_ptr<ResultType> mResult;

public:
  explicit Private(AsyncQuery& aParent)
    : mParent(aParent)
  {}

  Private(AsyncQuery& aParent, std::shared_ptr<std::istream> aData)
    : mParent(aParent)
    , mDataSource(std::move(aData))
  {}

  Private(AsyncQuery& aParent, std::string aLink)
    : mParent(aParent)
    , mDataSource(std::in_place_type<AsyncDownload>)
  {
    std::get<AsyncDownload>(mDataSource).SetLink(std::move(aLink));
  }

  Private(const Private& aOther) = delete;
  Private& operator=(const Private& aOther) = delete;

  ~Private()
  {
    for (sigc::connection& elem : mConnectionList)
      elem.disconnect();
  }

  const std::string& GetErrorMessage() const { return mErrorMessage.value(); }
  std::shared_ptr<ResultType> GetResult() const { return mResult; }

  void Enqueue(Mode aMode)
  {
    if (mDataSource.index() == 0) {
      auto& download = std::get<AsyncDownload>(mDataSource);
      auto it1 = mConnectionList.emplace(mConnectionList.end());
      auto it2 = mConnectionList.emplace(mConnectionList.end());

      *it1 = download.Failed.connect(
        // Strictly normal signal/slot action here.
        [&, it1, it2](std::string aMessage) {
          it1->disconnect();
          it2->disconnect();
          mConnectionList.erase(it1);
          mConnectionList.erase(it2);
          mErrorMessage = std::move(aMessage);
          mParent.Failed(mErrorMessage.value());
        });

      *it2 = download.Finished.connect(
        // Strictly normal signal/slot action here.
        [&, aMode, it1, it2](std::shared_ptr<std::istream> aData) {
          it1->disconnect();
          it2->disconnect();
          mConnectionList.erase(it1);
          mConnectionList.erase(it2);
          mDataSource = aData;
          OnDownloadFinished(aMode, std::move(aData));
        });

      download.Enqueue();
    } else if (mDataSource.index() == 1) {
      OnDownloadFinished(aMode, std::get<1>(mDataSource));
    }
  }

  void SetData(std::shared_ptr<std::istream> aNewValue)
  {
    mDataSource = std::move(aNewValue);
  }

  void SetLink(std::string aNewValue)
  {
    auto& ref = mDataSource.emplace<AsyncDownload>();
    ref.SetLink(std::move(aNewValue));
  }

private:
  void OnDownloadFinished(Mode aMode, std::shared_ptr<std::istream> aData)
  {
    auto task = std::make_unique<WorkerThread::QueryTask>();
    auto it1 = mConnectionList.emplace(mConnectionList.end());
    auto it2 = mConnectionList.emplace(mConnectionList.end());
    task->File = std::move(aData);
    task->Mode = aMode;

    *it1 = task->Failed.connect(
      // The main loop will delete the signal.
      [this, it1, it2](std::string aMessage) {
        mConnectionList.erase(it1);
        mConnectionList.erase(it2);
        mErrorMessage = std::move(aMessage);
        mParent.Failed(mErrorMessage.value());
      });

    *it2 = task->Finished.connect(
      // The main loop will delete the signal.
      [this, it1, it2](ResultType aBuffer) {
        mConnectionList.erase(it1);
        mConnectionList.erase(it2);
        mResult = std::make_shared<ResultType>(std::move(aBuffer));
        mParent.Finished(mResult);
      });

    gThread->Enqueue(std::move(task));
  }
};

AsyncQuery::AsyncQuery()
  : mPrivate(new Private(*this))
{}

AsyncQuery::AsyncQuery(std::shared_ptr<std::istream> aData)
  : mPrivate(new Private(*this, std::move(aData)))
{}

AsyncQuery::AsyncQuery(std::string aLink)
  : mPrivate(new Private(*this, std::move(aLink)))
{}

AsyncQuery::~AsyncQuery() = default;

const std::string&
AsyncQuery::GetErrorMessage() const
{
  return mPrivate->GetErrorMessage();
}

const std::shared_ptr<AsyncQuery::ResultType>
AsyncQuery::GetResult() const
{
  return mPrivate->GetResult();
}

void
AsyncQuery::Enqueue(Mode aMode)
{
  return mPrivate->Enqueue(aMode);
}

void
AsyncQuery::SetData(std::shared_ptr<std::istream> aNewValue)
{
  mPrivate->SetData(std::move(aNewValue));
}

void
AsyncQuery::SetLink(std::string aNewValue)
{
  mPrivate->SetLink(std::move(aNewValue));
}
