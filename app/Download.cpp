#include "Download.hpp"

#include <cassert>
#include <cstdlib>
#include <mutex>
#include <ostream>
#include <queue>
#include <thread>
#include <unordered_set>
#include <utility>

#include <curl/curl.h>

#ifdef _MSC_VER
#else
#include <stdio.h>
#include <unistd.h>
#endif

#include "Main.hpp"

namespace {

#ifdef _MSC_VER
#else

class TempBuffer : public std::streambuf
{
  std::string mPath;
  char mBuffer;
  int mFile;

public:
  TempBuffer()
  {
    char buffer[] = "tempXXXXXX";
    mFile = mkstemp(buffer);
    mPath = buffer;
  }

  ~TempBuffer() override
  {
    close(mFile);
    remove(mPath.c_str());
  }

  int overflow(int aValue) override
  {
    auto buffer = static_cast<char>(aValue);

    if (write(mFile, &buffer, 1) == 1)
      return aValue;
    else
      return traits_type::eof();
  }

  int underflow() override
  {
    if (read(mFile, &mBuffer, 1) == 1) {
      setg(&mBuffer, &mBuffer, &mBuffer + 1);
      return static_cast<unsigned char>(mBuffer);
    } else {
      return traits_type::eof();
    }
  }

  std::streampos seekoff(std::streamoff aOffset,
                         std::ios_base::seekdir aDirection,
                         std::ios_base::openmode aWhich) override
  {
    (void)aWhich;
    int whence = SEEK_SET;

    switch (aDirection) {
      case std::ios_base::beg:
        whence = SEEK_SET;
        break;
      case std::ios_base::end:
        whence = SEEK_END;
        break;
      case std::ios_base::cur:
        whence = SEEK_CUR;
        break;
    }

    return lseek(mFile, aOffset, whence);
  }

  std::streampos seekpos(std::streampos aPosition,
                         std::ios_base::openmode aWhich) override
  {
    (void)aWhich;
    return lseek(mFile, aPosition, SEEK_SET);
  }
};

#endif

/// Special stream that deletes its file automatically.
class TempFile : public std::iostream
{

  TempBuffer mBuffer;

public:
  TempFile() { rdbuf(&mBuffer); }
};

struct DownloadTask
{
  std::string ResourceLink;
  Signal<std::string> Failed;
  Signal<std::unique_ptr<std::istream>> Finished;
};

class DownloadThread
{
  /// Whether the main loop for the thread should exit.
  bool mRunning;
  /// All downloads and i/o will happen on this thread.
  std::thread mThread;
  /// Synchronize access to the transfer queue.
  std::mutex mMutex;
  /// Sequence of transfers to be submitted to CURL.
  std::queue<std::unique_ptr<DownloadTask>> mQueue;
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

  void Enqueue(std::unique_ptr<DownloadTask> aTask)
  {
    std::unique_lock<std::mutex> lock(mMutex);
    mQueue.emplace(std::move(aTask));
    curl_multi_wakeup(mLibrary);
  }

private:
  struct TaskState
  {
    std::unique_ptr<TempFile> File;
    std::unique_ptr<DownloadTask> Task;
  };

  /// Callback function for CURL writes.
  static size_t WriteProc(char* src, size_t a, size_t b, void* st)
  {
    auto progress = reinterpret_cast<TaskState*>(st);
    size_t count = a * b;
    progress->File->write(src, count);
    return progress->File->good() ? count : 0;
  }

  void CompleteWithFailure(TaskState& aState, std::string aMessage)
  {
    auto signal = aState.Task.release();

    InvokeAsync([aMessage, signal]() {
      signal->Failed.Notify(aMessage);
      delete signal;
    });
  }

  void CompleteWithSuccess(TaskState& aState)
  {
    auto stream = aState.File.release();
    auto signal = aState.Task.release();

    stream->flush();
    stream->seekg(0, std::ios_base::beg);
    stream->clear();

    InvokeAsync([stream, signal]() {
      std::unique_ptr<std::istream> ptr(stream);
      signal->Finished.Notify(std::move(ptr));
      delete signal;
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

        TaskState* state;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &state);

        if (msg->data.result == CURLE_OK) {
          long code, protocol;
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
      TaskState* state;
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
      auto state = new TaskState;
      state->File = std::make_unique<TempFile>();
      state->Task = std::move(save.front());

      auto easy = curl_easy_init();
      curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(easy, CURLOPT_PRIVATE, state);
      curl_easy_setopt(easy, CURLOPT_WRITEDATA, state);
      curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, WriteProc);

      auto url = state->Task->ResourceLink.c_str();
      curl_easy_setopt(easy, CURLOPT_URL, url);
      curl_multi_add_handle(mLibrary, easy);

      aWorkingSet.emplace(easy);
      save.pop();
    }
  }
};

DownloadThread* gThread = nullptr;

}

void
ForkDownloadThread()
{
  assert(!gThread);

  if (curl_global_init(CURL_GLOBAL_ALL) != 0)
    std::terminate();

  gThread = new DownloadThread;
}

void
JoinDownloadThread()
{
  assert(gThread);
  delete gThread;
  gThread = nullptr;
  curl_global_cleanup();
}

Download::Download(std::string aResourceLink)
  : mResourceLink(std::move(aResourceLink))
{}

Download::~Download()
{
  for (auto& elem : mConnectionList)
    elem.Disconnect();
}

const std::string&
Download::GetResourceLink() const
{
  return mResourceLink;
}

std::string&
Download::GetResourceLink()
{
  return mResourceLink;
}

const std::string&
Download::GetErrorMessage() const
{
  return mErrorMessage;
}

const std::unique_ptr<std::istream>&
Download::GetInputStream() const
{
  return mInputStream;
}

std::unique_ptr<std::istream>&
Download::GetInputStream()
{
  return mInputStream;
}

void
Download::Launch()
{
  if (mResourceLink.empty()) {
    mErrorMessage = "Resource link is empty";
    Failed.Notify();
  } else {
    auto task = std::make_unique<DownloadTask>();
    auto it1 = mConnectionList.emplace(mConnectionList.end());
    auto it2 = mConnectionList.emplace(mConnectionList.end());
    task->ResourceLink = mResourceLink;

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
      [this, it1, it2](std::unique_ptr<std::istream> aFile) {
        mConnectionList.erase(it1);
        mConnectionList.erase(it2);
        mInputStream = std::move(aFile);
        Finished.Notify();
      });

    gThread->Enqueue(std::move(task));
  }
}
