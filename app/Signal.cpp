#include "Signal.hpp"

#include <cassert>

Connection::Connection()
  : mParent(nullptr)
{}

Connection::Connection(SlotBase& aParent)
  : Connection()
{
  Attach(aParent);
}

Connection::Connection(const Connection& aOther)
  : Connection()
{
  *this = aOther;
}

Connection::Connection(Connection&& aOther) noexcept
  : Connection()
{
  *this = std::move(aOther);
}

Connection&
Connection::operator=(const Connection& aOther)
{
  if (this != &aOther) {
    if (aOther.mParent)
      Attach(*aOther.mParent);
    else
      Detach();
  }

  return *this;
}

Connection&
Connection::operator=(Connection&& aOther) noexcept
{
  std::swap(mParent, aOther.mParent);
  std::swap(mParentIterator, aOther.mParentIterator);

  if (mParent)
    *mParentIterator = *this;
  if (aOther.mParent)
    *aOther.mParentIterator = aOther;

  return *this;
}

Connection::~Connection()
{
  Detach();
}

void
Connection::Attach(SlotBase& aParent)
{
  if (mParent == &aParent)
    return;

  Detach();
  mParent = &aParent;
  mParent->ConnectionList.emplace_back(*this);
  mParentIterator = std::prev(mParent->ConnectionList.end());
}

void
Connection::Detach()
{
  if (!mParent)
    return;

  mParent->ConnectionList.erase(mParentIterator);
  mParent = nullptr;
}

void
Connection::Disconnect()
{
  using SignalBase = detail::signal::SignalBase;

  if (!mParent)
    return;

  SignalBase& signalSave = mParent->Parent;
  SlotBase& slotSave = *mParent;

  // Destroy the slot and cooperate with the notification algorithm.
  {
    auto next = signalSave.SlotList.erase(slotSave.ParentIterator);

    // This is what allows callbacks to disconnect during traversal.
    for (auto& it : signalSave.IteratorStack)
      if (next == it)
        it = next;
  }

  assert(!mParent);
}

namespace detail::signal {

SlotBase::SlotBase(SignalBase& aParent)
  : Parent(aParent)
{}

SlotBase::~SlotBase()
{
  auto it = ConnectionList.begin();

  while (it != ConnectionList.end()) {
    auto save = it++;
    save->get().Detach();
  }

  assert(ConnectionList.empty());
}

}
