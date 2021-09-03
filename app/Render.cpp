#include "Render.hpp"

#include <cassert>
#include <exception>
#include <optional>
#include <utility>

#include <SDL_ttf.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

//===========================================================================//
//=== Texture ===============================================================//
//===========================================================================//

Texture::Texture()
  : mAspectRatio(0.0f)
  , mWidth(0)
  , mHeight(0)
{
  glGenTextures(1, &mHandle);

  GLint prev;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
  glBindTexture(GL_TEXTURE_2D, mHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, prev);
}

Texture::Texture(const char* aText)
  : Texture()
{
  StrokeText(aText);
}

Texture::Texture(const SDL_Surface& aBuffer)
  : Texture()
{
  LoadImage(aBuffer);
}

Texture::Texture(Texture&& aOther) noexcept
  : Texture()
{
  *this = std::move(aOther);
}

Texture&
Texture::operator=(Texture&& aOther) noexcept
{
  std::swap(mHandle, aOther.mHandle);
  std::swap(mAspectRatio, aOther.mAspectRatio);
  std::swap(mWidth, aOther.mWidth);
  std::swap(mHeight, aOther.mHeight);
  return *this;
}

Texture::~Texture()
{
  if (mHandle)
    glDeleteTextures(1, &mHandle);
}

Texture::operator GLuint() const
{
  return mHandle;
}

float
Texture::GetAspectRatio() const
{
  return mAspectRatio;
}

unsigned
Texture::GetWidth() const
{
  return mWidth;
}

unsigned
Texture::GetHeight() const
{
  return mHeight;
}

void
Texture::LoadImage(const SDL_Surface& aImage)
{
  std::optional<GLint> internal;
  std::optional<GLenum> format;
  std::optional<GLenum> type;

  switch (aImage.format->format) {
    case SDL_PIXELFORMAT_RGBA8888:
      internal = GL_RGBA;
      format = GL_RGBA;
      type = GL_UNSIGNED_BYTE;
    case SDL_PIXELFORMAT_ABGR8888:
      internal = GL_RGBA;
      format = GL_RGBA;
      type = GL_UNSIGNED_INT_8_8_8_8_REV;
      break;
    case SDL_PIXELFORMAT_ARGB8888:
      internal = GL_RGBA;
      format = GL_BGRA;
      type = GL_UNSIGNED_INT_8_8_8_8_REV;
      break;
    case SDL_PIXELFORMAT_BGRA8888:
      internal = GL_RGBA;
      format = GL_BGRA;
      type = GL_UNSIGNED_BYTE;
      break;
    case SDL_PIXELFORMAT_RGB24:
      internal = GL_RGB;
      format = GL_RGB;
      type = GL_UNSIGNED_BYTE;
      break;
    case SDL_PIXELFORMAT_BGR24:
      internal = GL_RGB;
      format = GL_BGR;
      type = GL_UNSIGNED_BYTE;
      break;

    default:
      break;
  };

  GLint prev;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
  glBindTexture(GL_TEXTURE_2D, mHandle);

  glTexImage2D(
    // Will trigger assertion if options are not filled.
    GL_TEXTURE_2D,
    0,
    internal.value(),
    aImage.w,
    aImage.h,
    0,
    format.value(),
    type.value(),
    aImage.pixels);

  glBindTexture(GL_TEXTURE_2D, prev);

  mWidth = aImage.w;
  mHeight = aImage.h;
  mAspectRatio = static_cast<float>(mWidth) / mHeight;
}

namespace {

TTF_Font* gFont = nullptr;

}

void
Texture::StrokeText(const char* aText)
{
  SDL_Color fg{ 0xFF, 0xFF, 0xFF, 0xFF };
  SDL_Surface* surface = TTF_RenderText_Blended(gFont, aText, fg);

  if (!surface) {
    SDL_LogCritical(0, "TTF error: %s", TTF_GetError());
    return;
  }

  GLint prev;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
  glBindTexture(GL_TEXTURE_2D, mHandle);
  assert(surface->format->format == SDL_PIXELFORMAT_ARGB8888);

  glTexImage2D(
    // Should result in grayscale image.
    GL_TEXTURE_2D,
    0,
    GL_ALPHA,
    surface->w,
    surface->h,
    0,
    GL_BGRA,
    GL_UNSIGNED_INT_8_8_8_8_REV,
    surface->pixels);

  glBindTexture(GL_TEXTURE_2D, prev);

  mWidth = surface->w;
  mHeight = surface->h;
  mAspectRatio = static_cast<float>(mWidth) / mHeight;
  SDL_FreeSurface(surface);
}

//===========================================================================//
//=== RenderOptions =========================================================//
//===========================================================================//

RenderOptions::RenderOptions()
  : BoundingBoxEnable(false)
  , BoundingBoxColor(1.0f, 1.0f, 1.0f, 1.0f)
{}

//===========================================================================//
//=== RenderNode ============================================================//
//===========================================================================//

RenderNode::RenderNode(int aType)
  : mType(aType)
  , mParent(nullptr)
  , mLocalTransform(glm::identity<glm::mat4>())
{}

int
RenderNode::GetType() const
{
  return mType;
}

const RenderNode*
RenderNode::GetParent() const
{
  return mParent;
}

RenderNode*
RenderNode::GetParent()
{
  return mParent;
}

const SDL_FRect&
RenderNode::GetLocalBounds() const
{
  if (!mLocalBounds.has_value())
    mLocalBounds.emplace(ImplLocalBounds());

  return mLocalBounds.value();
}

const glm::mat4&
RenderNode::GetLocalTransform() const
{
  return mLocalTransform;
}

void
RenderNode::SetLocalTransform(const glm::mat4& aNewValue)
{
  mLocalTransform = aNewValue;
  DirtyBounds();
}

void
RenderNode::Draw(const RenderOptions& aOptions)
{
  glm::mat4 modelView = glm::identity<glm::mat4>();

  for (auto ptr = mParent; ptr; ptr = ptr->mParent)
    modelView = ptr->GetLocalTransform() * modelView;

  Draw(modelView, aOptions);
}

namespace {

SDL_bool
SDL_RectEmpty(const SDL_FRect* r)
{
  return ((!r) || (r->w <= 0) || (r->h <= 0)) ? SDL_TRUE : SDL_FALSE;
}

SDL_bool
SDL_HasIntersection(const SDL_FRect* A, const SDL_FRect* B)
{
  int Amin, Amax, Bmin, Bmax;

  if (!A) {
    SDL_InvalidParamError("A");
    return SDL_FALSE;
  }

  if (!B) {
    SDL_InvalidParamError("B");
    return SDL_FALSE;
  }

  /* Special cases for empty rects */
  if (SDL_RectEmpty(A) || SDL_RectEmpty(B)) {
    return SDL_FALSE;
  }

  /* Horizontal intersection */
  Amin = A->x;
  Amax = Amin + A->w;
  Bmin = B->x;
  Bmax = Bmin + B->w;
  if (Bmin > Amin)
    Amin = Bmin;
  if (Bmax < Amax)
    Amax = Bmax;
  if (Amax <= Amin)
    return SDL_FALSE;

  /* Vertical intersection */
  Amin = A->y;
  Amax = Amin + A->h;
  Bmin = B->y;
  Bmax = Bmin + B->h;
  if (Bmin > Amin)
    Amin = Bmin;
  if (Bmax < Amax)
    Amax = Bmax;
  if (Amax <= Amin)
    return SDL_FALSE;

  return SDL_TRUE;
}

}

void
RenderNode::Draw(const glm::mat4& aModelView, const RenderOptions& aOptions)
{
  SDL_FRect bounds = GetLocalBounds();
  bounds = TransformBounds(bounds, aModelView);

  {
    SDL_FRect view{ -1.0f, -1.0f, 2.0f, 2.0f };

    if (SDL_HasIntersection(&bounds, &view))
      Visited.Notify();
    else
      return;
  }

  ImplDraw(aModelView * mLocalTransform, aOptions);

  if (aOptions.BoundingBoxEnable) {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(bounds.x, bounds.y, 0.0f);
    glScalef(bounds.w, bounds.h, 1.0f);
    glBegin(GL_LINE_LOOP);
    glColor4fv(glm::value_ptr(aOptions.BoundingBoxColor));
    glVertex2f(0.0f, 0.0f);
    glVertex2f(1.0f, 0.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();
  }
}

int
RenderNode::AllocType()
{
  static int counter = 0;
  return counter++;
}

void
RenderNode::Adopt(RenderNode& aChild)
{
  assert(!aChild.mParent);
  aChild.mParent = this;
}

void
RenderNode::Disown(RenderNode& aChild)
{
  assert(aChild.mParent == this);
  aChild.mParent = nullptr;
}

void
RenderNode::DirtyBounds()
{
  for (RenderNode* cursor = this; cursor; cursor = cursor->mParent)
    cursor->mLocalBounds.reset();
}

//===========================================================================//
//=== GeomNode ==============================================================//
//===========================================================================//

const int GeomNode::TypeId = AllocType();

GeomNode::GeomNode()
  : RenderNode(TypeId)
  , mDrawMode(GL_POLYGON)
  , mTexture(nullptr)
  , mGeometryLock(false)
{
  Vertex tmp;
  tmp.Color = { 0.7f, 0.0f, 0.7f, 1.0f };

  // Default geometry is the unit square.
  tmp.Location = { 0.0f, 0.0f };
  tmp.TexCoord = { 0.0f, 1.0f };
  mGeometry.push_back(tmp);
  tmp.Location = { 1.0f, 0.0f };
  tmp.TexCoord = { 1.0f, 1.0f };
  mGeometry.push_back(tmp);
  tmp.Location = { 1.0f, 1.0f };
  tmp.TexCoord = { 1.0f, 0.0f };
  mGeometry.push_back(tmp);
  tmp.Location = { 0.0f, 1.0f };
  tmp.TexCoord = { 0.0f, 0.0f };
  mGeometry.push_back(tmp);
}

GLenum
GeomNode::GetDrawMode() const
{
  return mDrawMode;
}

void
GeomNode::SetDrawMode(GLenum aNewValue)
{
  mDrawMode = aNewValue;
}

const Texture*
GeomNode::GetTexture() const
{
  return mTexture;
}

Texture*
GeomNode::GetTexture()
{
  return mTexture;
}

void
GeomNode::SetTexture(Texture* aNewValue)
{
  mTexture = aNewValue;
}

const std::vector<Vertex>&
GeomNode::GetGeometry() const
{
  return mGeometry;
}

std::vector<Vertex>&
GeomNode::LockGeometry()
{
  assert(!mGeometryLock);
  mGeometryLock = true;
  return mGeometry;
}

void
GeomNode::UnlockGeometry()
{
  assert(mGeometryLock);
  mGeometryLock = false;
  DirtyBounds();
}

SDL_FRect
GeomNode::ImplLocalBounds() const
{
  if (mGeometry.empty())
    return SDL_FRect{ 0.0f, 0.0f, 0.0f, 0.0f };

  glm::vec2 maximum(mGeometry[0].Location[0], mGeometry[0].Location[1]);
  glm::vec2 minimum(mGeometry[0].Location[0], mGeometry[0].Location[1]);

  for (auto it = mGeometry.begin() + 1; it != mGeometry.end(); ++it)
    for (int j = 0; j < 2; ++j) {
      maximum[j] = std::max(maximum[j], it->Location[j]);
      minimum[j] = std::min(minimum[j], it->Location[j]);
    }

  SDL_FRect result;
  result.x = minimum.x;
  result.y = minimum.y;
  result.w = maximum.x - minimum.x;
  result.h = maximum.y - minimum.y;
  return TransformBounds(result, GetLocalTransform());
}

void
GeomNode::ImplDraw(const glm::mat4& aModelView,
                   const RenderOptions& aOptions) const
{
  (void)aOptions;
  assert(!mGeometryLock);

  if (mGeometry.empty())
    return;

  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixf(glm::value_ptr(aModelView));

  if (mTexture) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, *mTexture);
  }

  glBegin(mDrawMode);

  for (const Vertex& elem : mGeometry) {
    glColor4fv(glm::value_ptr(elem.Color));
    glTexCoord2fv(glm::value_ptr(elem.TexCoord));
    glVertex2fv(glm::value_ptr(elem.Location));
  }

  glEnd();
  glDisable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
}

//===========================================================================//
//=== GroupNode =============================================================//
//===========================================================================//

const int GroupNode::TypeId = AllocType();

GroupNode::GroupNode()
  : RenderNode(TypeId)
  , mChildrenLock(false)
{}

const decltype(GroupNode::mChildren)&
GroupNode::GetChildren() const
{
  return mChildren;
}

decltype(GroupNode::mChildren)&
GroupNode::LockChildren()
{
  assert(!mChildrenLock);
  mChildrenLock = true;
  return mChildren;
}

void
GroupNode::UnlockChildren()
{
  assert(mChildrenLock);
  mChildrenLock = false;
  DirtyBounds();
}

SDL_FRect
GroupNode::ImplLocalBounds() const
{
  assert(!mChildrenLock);

  if (mChildren.empty())
    return SDL_FRect{ 0.0f, 0.0f, 0.0f, 0.0f };

  glm::vec2 maximum;
  glm::vec2 minimum;

  {
    SDL_FRect bounds = mChildren[0].get().GetLocalBounds();
    maximum[0] = bounds.x + bounds.w;
    maximum[1] = bounds.y + bounds.h;
    minimum[0] = bounds.x;
    minimum[1] = bounds.y;
  }

  for (auto it = mChildren.begin() + 1; it != mChildren.end(); ++it)
    for (int j = 0; j < 2; ++j) {
      SDL_FRect bounds = it->get().GetLocalBounds();
      maximum[0] = std::max(maximum[0], bounds.x + bounds.w);
      maximum[1] = std::max(maximum[0], bounds.y + bounds.h);
      minimum[0] = std::min(minimum[0], bounds.x);
      minimum[1] = std::min(minimum[1], bounds.y);
    }

  SDL_FRect result;
  result.x = minimum.x;
  result.y = minimum.y;
  result.w = maximum.x - minimum.x;
  result.h = maximum.y - minimum.y;
  return TransformBounds(result, GetLocalTransform());
}

void
GroupNode::ImplDraw(const glm::mat4& aModelView,
                    const RenderOptions& aOptions) const
{
  if (mChildren.empty())
    return;

  for (std::reference_wrapper<RenderNode> child : mChildren)
    child.get().Draw(aModelView, aOptions);
}

//===========================================================================//
//=== TextNode ==============================================================//
//===========================================================================//

const int TextNode::TypeId = AllocType();

TextNode::TextNode()
  : RenderNode(TypeId)
  , mColor(1.0f, 1.0f, 1.0f, 1.0f)
  , mTexture(nullptr)
{}

const glm::vec4&
TextNode::GetColor() const
{
  return mColor;
}

void
TextNode::SetColor(const glm::vec4& aNewValue)
{
  mColor = aNewValue;
}

const Texture*
TextNode::GetTexture() const
{
  return mTexture;
}

Texture*
TextNode::GetTexture()
{
  return mTexture;
}

void
TextNode::SetTexture(Texture* aNewValue)
{
  mTexture = aNewValue;
}

SDL_FRect
TextNode::ImplLocalBounds() const
{
  SDL_FRect result{ 0.0f, 0.0f, 1.0f, 1.0f };
  return TransformBounds(result, GetLocalTransform());
}

void
TextNode::ImplDraw(const glm::mat4& aModelView,
                   const RenderOptions& aOptions) const
{
  (void)aOptions;

  if (!mTexture)
    return;

  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixf(glm::value_ptr(aModelView));

  glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT);
  glEnable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glBindTexture(GL_TEXTURE_2D, *mTexture);

  glBegin(GL_POLYGON);
  glColor4fv(glm::value_ptr(mColor));
  glTexCoord2f(0.0f, 0.0f);
  glVertex2f(0.0f, 0.0f);
  glTexCoord2f(1.0f, 0.0f);
  glVertex2f(1.0f, 0.0f);
  glTexCoord2f(1.0f, 1.0f);
  glVertex2f(1.0f, 1.0f);
  glTexCoord2f(0.0f, 1.0f);
  glVertex2f(0.0f, 1.0f);
  glEnd();

  glPopAttrib();
}

//===========================================================================//
//=== Functions =============================================================//
//===========================================================================//

extern const unsigned char CascadiaCode_Regular_ttf_data[];
extern const size_t CascadiaCode_Regular_ttf_size;

void
InitRenderer()
{
  assert(!gFont);

  if (TTF_Init() != 0)
    goto error;

  {
    SDL_RWops* ops = SDL_RWFromConstMem(
      // Stored in executable as resource.
      CascadiaCode_Regular_ttf_data,
      CascadiaCode_Regular_ttf_size);

    gFont = TTF_OpenFontRW(ops, 1, 256);
  }

  if (gFont)
    return;

error:
  SDL_LogCritical(0, "TTF error: %s", TTF_GetError());
  std::terminate();
}

void
FreeRenderer()
{
  assert(gFont);
  TTF_CloseFont(gFont);
  TTF_Quit();
  gFont = nullptr;
}

SDL_FRect
TransformBounds(const SDL_FRect& aBounds, const glm::mat4& aTransform)
{
  glm::vec4 vertices[4];
  vertices[0].x = aBounds.x;
  vertices[0].y = aBounds.y;
  vertices[0].z = 0.0f;
  vertices[0].w = 1.0f;
  vertices[1].x = aBounds.x + aBounds.w;
  vertices[1].y = aBounds.y;
  vertices[1].z = 0.0f;
  vertices[1].w = 1.0f;
  vertices[2].x = aBounds.x + aBounds.w;
  vertices[2].y = aBounds.y + aBounds.h;
  vertices[2].z = 0.0f;
  vertices[2].w = 1.0f;
  vertices[3].x = aBounds.x;
  vertices[3].y = aBounds.y + aBounds.h;
  vertices[3].z = 0.0f;
  vertices[3].w = 1.0f;

  for (int i = 0; i < 4; ++i)
    vertices[i] = aTransform * vertices[i];

  glm::vec2 maximum(vertices[0][0], vertices[0][1]);
  glm::vec2 minimum(vertices[0][0], vertices[0][1]);

  for (int i = 1; i < 4; ++i)
    for (int j = 0; j < 2; ++j) {
      maximum[j] = std::max(maximum[j], vertices[i][j]);
      minimum[j] = std::min(minimum[j], vertices[i][j]);
    }

  SDL_FRect result;
  result.x = minimum.x;
  result.y = minimum.y;
  result.w = maximum.x - minimum.x;
  result.h = maximum.y - minimum.y;
  return result;
}
