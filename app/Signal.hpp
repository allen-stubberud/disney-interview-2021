#ifndef SIGNAL_HPP
#define SIGNAL_HPP

#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <utility>
#include <vector>

namespace detail::signal {

struct SlotBase;

/// Required data for all signals.
struct SignalBase
{
  SignalBase() = default;
  SignalBase(const SignalBase& aOther) = delete;
  SignalBase& operator=(const SignalBase& aOther) = delete;

  std::list<std::unique_ptr<SlotBase>> SlotList;
  std::vector<decltype(SlotList)::iterator> IteratorStack;
};

}

class Connection;

namespace detail::signal {

/// Required data for all slots; intended to be subclassed.
struct SlotBase
{
  explicit SlotBase(SignalBase& aParent);
  SlotBase(const SlotBase& aOther) = delete;
  SlotBase& operator=(const SlotBase& aOther) = delete;
  virtual ~SlotBase();

  SignalBase& Parent;
  decltype(SignalBase::SlotList)::iterator ParentIterator;
  std::list<std::reference_wrapper<Connection>> ConnectionList;
};

}

class Connection
{
  using SlotBase = detail::signal::SlotBase;

  SlotBase* mParent;
  decltype(SlotBase::ConnectionList)::iterator mParentIterator;

public:
  Connection();
  explicit Connection(SlotBase& aParent);
  Connection(const Connection& aOther);
  Connection(Connection&& aOther) noexcept;
  Connection& operator=(const Connection& aOther);
  Connection& operator=(Connection&& aOther) noexcept;
  ~Connection();

  void Detach();
  void Disconnect();

private:
  void Attach(SlotBase& aParent);
};

template<typename... Args>
class Signal
{
  using SignalBase = detail::signal::SignalBase;
  using SlotBase = detail::signal::SlotBase;

  SignalBase mState;

public:
  Signal() = default;
  Signal(const Signal& aOther) = delete;
  Signal& operator=(const Signal& aOther) = delete;

private:
  struct SlotData : public SlotBase
  {
    explicit SlotData(SignalBase& aParent)
      : SlotBase(aParent)
    {}

    std::function<void(Args...)> Callback;
  };

public:
  template<typename T>
  Connection Connect(T&& aFunctor)
  {
    auto slot = new SlotData(mState);
    mState.SlotList.emplace_back(slot);
    slot->Callback = std::forward<T>(aFunctor);
    slot->ParentIterator = std::prev(mState.SlotList.end());
    return Connection(*slot);
  }

  template<typename... T>
  void Notify(T&&... aArgList)
  {
    auto idx = mState.IteratorStack.size();
    mState.IteratorStack.push_back(mState.SlotList.begin());

    while (mState.IteratorStack[idx] != mState.SlotList.end()) {
      auto save = mState.IteratorStack[idx]++;
      static_cast<SlotData&>(**save).Callback(std::forward<T>(aArgList)...);
    }

    mState.IteratorStack.pop_back();
  }
};

#endif
