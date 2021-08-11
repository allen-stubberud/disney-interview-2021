#include "Image.hpp"

#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <utility>

#include <SDL_image.h>

#include "Main.hpp"

namespace {

struct ImageTask
{
  std::unique_ptr<std::istream> Stream;
  Signal<const std::string&> Failed;
  Signal<SDL_Surface*&> Finished;
};

class ImageThread
{
  /// Whether the main loop for the thread should exit.
  bool mRunning;
  /// All downloads and i/o will happen on this thread.
  std::thread mThread;
  /// Synchronize access to the transfer queue.
  std::mutex mMutex;
  /// Allow the thread to wait for more inputs.
  std::condition_variable mCondition;
  /// Sequence of image files to be decoded.
  std::queue<std::unique_ptr<ImageTask>> mQueue;

public:
  ImageThread()
    : mRunning(true)
  {
    mThread = std::thread(std::bind(&ImageThread::MainLoop, this));
  }

  ImageThread(const ImageThread& aOther) = delete;
  ImageThread& operator=(const ImageThread& aOther) = delete;

  ~ImageThread()
  {
    mRunning = false;
    mCondition.notify_all();
    mThread.join();
  }

  void Enqueue(std::unique_ptr<ImageTask> aTask)
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
      if (job) {
        auto signal = job->release();
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
            SDL_Surface* copy = result;
            signal->Finished.Notify(copy);
            SDL_FreeSurface(copy);
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
    }
  }
};

ImageThread* gThread = nullptr;

}

void
Image::ForkThread()
{
  assert(!gThread);
  gThread = new ImageThread;
}

void
Image::JoinThread()
{
  assert(gThread);
  delete gThread;
  gThread = nullptr;
  IMG_Quit();
}

Image::Image()
{
  // Bind is not possible because Notify() is template function.
  mDownload.Failed.Connect(
    [this](const std::string& aMessage) { Failed.Notify(aMessage); });

  // Bind is unwieldly because of the overload.
  mDownload.Finished.Connect(
    [this](std::unique_ptr<std::istream>& aInput) { Launch(aInput); });
}

Image::Image(std::string aResourceLink)
  : Image()
{
  mDownload.GetResourceLink() = std::move(aResourceLink);
}

Image::~Image()
{
  for (auto& elem : mConnectionList)
    elem.Disconnect();
}

const std::string&
Image::GetResourceLink() const
{
  return mDownload.GetResourceLink();
}

std::string&
Image::GetResourceLink()
{
  return mDownload.GetResourceLink();
}

void
Image::Launch()
{
  mDownload.Launch();
}

void
Image::Launch(std::unique_ptr<std::istream>& aInput)
{
  auto task = std::make_unique<ImageTask>();
  auto it1 = mConnectionList.emplace(mConnectionList.end());
  auto it2 = mConnectionList.emplace(mConnectionList.end());
  task->Stream = std::move(aInput);

  *it1 = task->Failed.Connect(
    // The main loop will delete the signal.
    [this, it1, it2](const std::string& aMessage) {
      mConnectionList.erase(it1);
      mConnectionList.erase(it2);
      Failed.Notify(aMessage);
    });

  *it2 = task->Finished.Connect(
    // The main loop will delete the signal.
    [this, it1, it2](SDL_Surface* aImage) {
      mConnectionList.erase(it1);
      mConnectionList.erase(it2);
      Finished.Notify(aImage);
    });

  gThread->Enqueue(std::move(task));
}
