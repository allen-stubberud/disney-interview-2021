#include "Network.hpp"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <list>
#include <mutex>
#include <optional>
#include <ostream>
#include <queue>
#include <streambuf>
#include <thread>
#include <unordered_set>
#include <utility>

#include <SDL.h>
#include <curl/curl.h>

#ifdef _MSC_VER
#else
#include <stdio.h>
#include <unistd.h>
#endif

#include "Main.hpp"

//===========================================================================//
//=== Stream ================================================================//
//===========================================================================//

namespace {

#ifdef _MSC_VER
#error
#else

/// Special stream that deletes its file automatically.
class TempFile : public std::iostream
{
  std::string mPath;
  std::filebuf mBufferImpl;

public:
  TempFile()
  {
    char buffer[] = "tempXXXXXX";
    close(mkstemp(buffer));

    mPath = buffer;
    mBufferImpl.open(buffer, in | out | binary);
    rdbuf(&mBufferImpl);
  }

  ~TempFile()
  {
    mBufferImpl.close();
    remove(mPath.c_str());
  }
};

#endif

}

//===========================================================================//
//=== Thread ================================================================//
//===========================================================================//

namespace {

class DownloadThread
{
  /// Whether the main loop for the thread should exit.
  bool mRunning;
  /// All downloads and i/o will happen on this thread.
  std::thread mThread;
  /// Synchronize access to the transfer queue.
  std::mutex mMutex;

public:
  struct Task
  {
    std::string ResourceLink;
    sigc::signal<void(std::string)> Failed;
    sigc::signal<void(std::shared_ptr<std::istream>)> Finished;
  };

private:
  /// Sequence of transfers to be submitted to CURL.
  std::queue<std::unique_ptr<Task>> mQueue;
  /// Store the single CURL multi context.
  CURLM* mLibrary;

public:
  DownloadThread()
    : mRunning(true)
  {
    mLibrary = curl_multi_init();
    mThread = std::thread(std::bind(&DownloadThread::MainLoop, this));
  }

  DownloadThread(const DownloadThread& aOther) = delete;
  DownloadThread& operator=(const DownloadThread& aOther) = delete;

  ~DownloadThread()
  {
    mRunning = false;
    curl_multi_wakeup(mLibrary);
    mThread.join();
    curl_multi_cleanup(mLibrary);
  }

  void Enqueue(std::unique_ptr<Task> aTask)
  {
    std::unique_lock<std::mutex> lock(mMutex);
    mQueue.emplace(std::move(aTask));
    curl_multi_wakeup(mLibrary);
  }

private:
  struct State
  {
    std::unique_ptr<TempFile> File;
    std::unique_ptr<Task> Task;
  };

  static size_t WriteProc(char* src, size_t a, size_t b, void* st)
  {
    auto progress = reinterpret_cast<State*>(st);
    size_t count = a * b;
    progress->File->write(src, count);
    return progress->File->good() ? count : 0;
  }

  void CompleteWithFailure(State& aState, std::string aMessage)
  {
    auto task = aState.Task.release();

    InvokeAsync([aMessage, task]() {
      task->Failed(std::move(aMessage));
      delete task;
    });
  }

  void CompleteWithSuccess(State& aState)
  {
    auto file = aState.File.release();
    auto task = aState.Task.release();

    file->flush();
    file->seekg(0, std::ios_base::beg);
    file->clear();

    InvokeAsync([file, task]() {
      task->Finished(std::shared_ptr<std::istream>(file));
      delete task;
    });
  }

  void MainLoop()
  {
    std::unordered_set<CURL*> jobs;

    while (mRunning) {
      SlurpQueue(jobs);

      int count;
      curl_multi_perform(mLibrary, &count);
      while (auto msg = curl_multi_info_read(mLibrary, &count)) {
        if (msg->msg != CURLMSG_DONE)
          continue;

        State* state;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &state);

        if (msg->data.result == CURLE_OK) {
          long code;
          curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &code);

          if (code == 200)
            CompleteWithSuccess(*state);
          else
            CompleteWithFailure(*state, std::to_string(code));
        } else {
          // The library provides formatted error messages.
          auto message = curl_easy_strerror(msg->data.result);
          CompleteWithFailure(*state, message);
        }

        delete state;
        jobs.erase(jobs.find(msg->easy_handle));
        curl_multi_remove_handle(mLibrary, msg->easy_handle);
        curl_easy_cleanup(msg->easy_handle);

        // This can be interrupted from another thread.
        curl_multi_poll(mLibrary, nullptr, 0, 1000, nullptr);
      }
    }

    // Clean up any remaining downloads retained by CURL.
    for (auto easy : jobs) {
      State* state;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &state);

      delete state;
      curl_multi_remove_handle(mLibrary, easy);
      curl_easy_cleanup(easy);
    }
  }

  /// Consume all elements in the queue (thread-safe).
  void SlurpQueue(std::unordered_set<CURL*>& aWorkingSet)
  {
    // Temporary storage for the queue.
    decltype(mQueue) save;

    // This is the fastest way to consume the whole queue.
    {
      std::unique_lock<std::mutex> lock(mMutex);
      std::swap(save, mQueue);
    }

    // Use the saved queue so the lock can be released.
    while (!save.empty()) {
      auto state = new State;
      state->File = std::make_unique<TempFile>();
      state->Task = std::move(save.front());

      auto easy = curl_easy_init();
      curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(easy, CURLOPT_PRIVATE, state);
      curl_easy_setopt(easy, CURLOPT_WRITEDATA, state);
      curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, WriteProc);
      curl_easy_setopt(easy, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);

      auto url = state->Task->ResourceLink.c_str();
      curl_easy_setopt(easy, CURLOPT_URL, url);
      curl_multi_add_handle(mLibrary, easy);

      aWorkingSet.emplace(easy);
      save.pop();
    }
  }
};

}

//===========================================================================//
//=== Globals ===============================================================//
//===========================================================================//

namespace {

DownloadThread* gThread = nullptr;

}

void
InitNetwork()
{
  assert(!gThread);

  // Global initialization function.
  {
    CURLcode status = curl_global_init(CURL_GLOBAL_ALL);
    assert(status == 0);
  }

  // Log the library version.
  {
    curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    SDL_Log("CURL version: %s", info->version);
  }

  gThread = new DownloadThread;
}

void
FreeNetwork()
{
  assert(gThread);
  delete gThread;
  gThread = nullptr;
  curl_global_cleanup();
}

//===========================================================================//
//=== AsyncDownload =========================================================//
//===========================================================================//

class AsyncDownload::Private
{
  AsyncDownload& mParent;
  /// Used to disconnect from signal early when this object dies.
  std::list<sigc::connection> mConnectionList;
  /// Passed directly to CURL to download the file.
  std::string mResourceLink;
  /// Error string if applicable.
  std::optional<std::string> mErrorMessage;
  /// Final data result if applicable.
  std::shared_ptr<std::istream> mResult;

public:
  explicit Private(AsyncDownload& aParent)
    : mParent(aParent)
  {}

  Private(AsyncDownload& aParent, std::string aLink)
    : mParent(aParent)
    , mResourceLink(std::move(aLink))
  {}

  Private(const Private& aOther) = delete;
  Private& operator=(const Private& aOther) = delete;

  ~Private()
  {
    for (sigc::connection& elem : mConnectionList)
      elem.disconnect();
  }

  const std::string& GetErrorMessage() const { return mErrorMessage.value(); }

  std::shared_ptr<std::istream> GetResult() const { return mResult; }

  void Enqueue()
  {
    if (mResourceLink.empty()) {
      mErrorMessage.emplace("Resource link is empty");
      mParent.Failed(mErrorMessage.value());
    } else {
      auto task = std::make_unique<DownloadThread::Task>();
      auto it1 = mConnectionList.emplace(mConnectionList.end());
      auto it2 = mConnectionList.emplace(mConnectionList.end());
      task->ResourceLink = mResourceLink;

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
        [this, it1, it2](std::shared_ptr<std::istream> aFile) {
          mConnectionList.erase(it1);
          mConnectionList.erase(it2);
          mResult = aFile;
          mParent.Finished(mResult);
        });

      gThread->Enqueue(std::move(task));
    }
  }

  void SetLink(std::string aNewValue) { mResourceLink = std::move(aNewValue); }
};

AsyncDownload::AsyncDownload()
  : mPrivate(new Private(*this))
{}

AsyncDownload::AsyncDownload(std::string aLink)
  : mPrivate(new Private(*this, std::move(aLink)))
{}

AsyncDownload::~AsyncDownload() = default;

const std::string&
AsyncDownload::GetErrorMessage() const
{
  return mPrivate->GetErrorMessage();
}

std::shared_ptr<std::istream>
AsyncDownload::GetResult() const
{
  return mPrivate->GetResult();
}

void
AsyncDownload::Enqueue()
{
  mPrivate->Enqueue();
}

void
AsyncDownload::SetLink(std::string aNewValue)
{
  mPrivate->SetLink(std::move(aNewValue));
}
