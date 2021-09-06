#include "Helper.hpp"

#include <cstddef>
#include <cstring>

//===========================================================================//
//=== Stream ================================================================//
//===========================================================================//

namespace {

size_t
CppToRW_Read(SDL_RWops* aStream, void* aBuffer, size_t aDimA, size_t aDimB)
{
  auto ptr = aStream->hidden.unknown.data1;
  auto stream = reinterpret_cast<std::istream*>(ptr);

  // This should automatically pick up error conditions.
  stream->read(reinterpret_cast<char*>(aBuffer), aDimA * aDimB);
  return stream->gcount() / aDimA;
}

Sint64
CppToRW_Seek(SDL_RWops* aStream, Sint64 aPos, int aWhence)
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

Sint64
CppToRW_Size(SDL_RWops* aStream)
{
  auto ptr = aStream->hidden.unknown.data1;
  auto stream = reinterpret_cast<std::istream*>(ptr);

  std::streamoff save = stream->tellg();
  stream->seekg(0, std::ios_base::end);
  std::streamoff size = stream->tellg();
  stream->seekg(save);
  return size;
}

int
CppToRW_Close_Owned(SDL_RWops* aStream)
{
  auto ptr = aStream->hidden.unknown.data1;
  delete reinterpret_cast<std::istream*>(ptr);
  SDL_FreeRW(aStream);
  return 0;
}

int
CppToRW_Close_Reference(SDL_RWops* aStream)
{
  SDL_FreeRW(aStream);
  return 0;
}

}

SDL_RWops*
CppToRW(std::istream& aFile)
{
  SDL_RWops* ops = SDL_AllocRW();
  memset(ops, 0, sizeof(SDL_RWops));
  ops->read = CppToRW_Read;
  ops->seek = CppToRW_Seek;
  ops->size = CppToRW_Size;
  ops->close = CppToRW_Close_Reference;
  ops->hidden.unknown.data1 = &aFile;
  return ops;
}

SDL_RWops*
CppToRW(std::unique_ptr<std::istream> aFile)
{
  SDL_RWops* ops = SDL_AllocRW();
  memset(ops, 0, sizeof(SDL_RWops));
  ops->read = CppToRW_Read;
  ops->seek = CppToRW_Seek;
  ops->size = CppToRW_Size;
  ops->close = CppToRW_Close_Owned;
  ops->hidden.unknown.data1 = aFile.release();
  return ops;
}
