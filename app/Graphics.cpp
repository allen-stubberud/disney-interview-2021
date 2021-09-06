#include "Graphics.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <vector>

#include <SDL_ttf.h>
#include <cmrc/cmrc.hpp>
#include <glm/gtc/type_ptr.hpp>

CMRC_DECLARE(rc);

//===========================================================================//
//=== Globals ===============================================================//
//===========================================================================//

namespace {

SDL_bool
SDL_RectEmpty(const SDL_FRect* r)
{
  return ((!r) || (r->w <= 0) || (r->h <= 0)) ? SDL_TRUE : SDL_FALSE;
}

SDL_bool
SDL_HasIntersection(const SDL_FRect* A, const SDL_FRect* B)
{
  float Amin, Amax, Bmin, Bmax;

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

void
SDL_UnionRect(const SDL_FRect* A, const SDL_FRect* B, SDL_FRect* result)
{
  float Amin, Amax, Bmin, Bmax;

  if (!A) {
    SDL_InvalidParamError("A");
    return;
  }

  if (!B) {
    SDL_InvalidParamError("B");
    return;
  }

  if (!result) {
    SDL_InvalidParamError("result");
    return;
  }

  /* Special cases for empty Rects */
  if (SDL_RectEmpty(A)) {
    if (SDL_RectEmpty(B)) {
      /* A and B empty */
      return;
    } else {
      /* A empty, B not empty */
      *result = *B;
      return;
    }
  } else {
    if (SDL_RectEmpty(B)) {
      /* A not empty, B empty */
      *result = *A;
      return;
    }
  }

  /* Horizontal union */
  Amin = A->x;
  Amax = Amin + A->w;
  Bmin = B->x;
  Bmax = Bmin + B->w;
  if (Bmin < Amin)
    Amin = Bmin;
  result->x = Amin;
  if (Bmax > Amax)
    Amax = Bmax;
  result->w = Amax - Amin;

  /* Vertical union */
  Amin = A->y;
  Amax = Amin + A->h;
  Bmin = B->y;
  Bmax = Bmin + B->h;
  if (Bmin < Amin)
    Amin = Bmin;
  result->y = Amin;
  if (Bmax > Amax)
    Amax = Bmax;
  result->h = Amax - Amin;
}

TTF_Font* gFont = nullptr;

}

void
InitGraphics()
{
  assert(!gFont);

  // Global initialization for font rendering.
  {
    int status = TTF_Init();
    assert(status == 0);
  }

  // Log the library verison with SDL methods.
  {
    const SDL_version* ver = TTF_Linked_Version();
    SDL_Log("TTF version: %d.%d.%d", ver->major, ver->minor, ver->patch);
  }

  // Actual font file is embedded in the executable.
  {
    auto fs = cmrc::rc::get_filesystem();
    auto buffer = fs.open("font.ttf");
    SDL_RWops* ops = SDL_RWFromConstMem(buffer.begin(), buffer.size());
    gFont = TTF_OpenFontRW(ops, 1, 256);
    assert(gFont);
  }
}

void
FreeGraphics()
{
  assert(gFont);
  TTF_CloseFont(gFont);
  TTF_Quit();
  gFont = nullptr;
}

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
//=== RenderNode ============================================================//
//===========================================================================//

RenderNode::RenderNode(int aType)
  : mType(aType)
  , mParentNode(nullptr)
  , mScale(1.0f, 1.0f)
  , mTranslate(0.0f, 0.0f)
{}

int
RenderNode::GetType() const
{
  return mType;
}

const RenderNode*
RenderNode::GetParent() const
{
  return mParentNode;
}

RenderNode*
RenderNode::GetParent()
{
  return mParentNode;
}

const glm::vec2&
RenderNode::GetScale() const
{
  return mScale;
}

const glm::vec2&
RenderNode::GetTranslate() const
{
  return mTranslate;
}

void
RenderNode::SetScale(const glm::vec2& aNewValue)
{
  mScale = aNewValue;
  DirtyBounds();
}

void
RenderNode::SetTranslate(const glm::vec2& aNewValue)
{
  mTranslate = aNewValue;
  DirtyBounds();
}

const SDL_FRect&
RenderNode::GetLocalBounds() const
{
  if (!mLocalBounds) {
    auto& ref = mLocalBounds.emplace();
    ImplLocalBounds(ref);

    ref.x *= mScale.x;
    ref.y *= mScale.y;
    ref.w *= mScale.x;
    ref.h *= mScale.y;

    ref.x += mTranslate.x;
    ref.y += mTranslate.y;
  }

  return mLocalBounds.value();
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
  assert(!aChild.mParentNode);
  aChild.mParentNode = this;
}

void
RenderNode::Disown(RenderNode& aChild)
{
  assert(aChild.mParentNode == this);
  aChild.mParentNode = nullptr;
}

void
RenderNode::DirtyBounds()
{
  for (auto cursor = this; cursor; cursor = cursor->mParentNode)
    cursor->mLocalBounds.reset();
}

//===========================================================================//
//=== ClipNode ==============================================================//
//===========================================================================//

const int ClipNode::TypeId = AllocType();

ClipNode::ClipNode()
  : RenderNode(TypeId)
  , mChildNode(nullptr)
  , mClipRect{ 0.0f, 0.0f, 1.0f, 1.0f }
{}

const RenderNode*
ClipNode::GetChild() const
{
  return mChildNode;
}

void
ClipNode::SetChild(RenderNode* aNewValue)
{
  if (mChildNode)
    Disown(*mChildNode);

  mChildNode = aNewValue;

  if (mChildNode)
    Adopt(*mChildNode);

  DirtyBounds();
}

const SDL_FRect&
ClipNode::GetClipRect() const
{
  return mClipRect;
}

void
ClipNode::SetClipRect(const SDL_FRect& aNewValue)
{
  mClipRect = aNewValue;
  DirtyBounds();
}

void
ClipNode::ImplLocalBounds(SDL_FRect& aBuffer) const
{
  aBuffer = mClipRect;
}

//===========================================================================//
//=== GeomNode ==============================================================//
//===========================================================================//

const int GeomNode::TypeId = AllocType();

GeomNode::GeomNode()
  : RenderNode(TypeId)
  , mDrawMode(GL_POLYGON)
  , mTexture(nullptr)
{
  Vertex tmp;
  tmp.Color = { 0.7f, 0.0f, 0.7f, 1.0f };

  // Default geometry is the unit square.
  tmp.Location = { 0.0f, 0.0f };
  tmp.TexCoord = { 0.0f, 0.0f };
  mGeometry.push_back(tmp);
  tmp.Location = { 1.0f, 0.0f };
  tmp.TexCoord = { 1.0f, 0.0f };
  mGeometry.push_back(tmp);
  tmp.Location = { 1.0f, 1.0f };
  tmp.TexCoord = { 1.0f, 1.0f };
  mGeometry.push_back(tmp);
  tmp.Location = { 0.0f, 1.0f };
  tmp.TexCoord = { 0.0f, 1.0f };
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

const std::vector<GeomNode::Vertex>&
GeomNode::GetGeometry() const
{
  return mGeometry;
}

std::vector<GeomNode::Vertex>&
GeomNode::GetGeometry()
{
  return mGeometry;
}

void
GeomNode::ImplLocalBounds(SDL_FRect& aBuffer) const
{
  if (mGeometry.empty()) {
    aBuffer.x = 0.0f;
    aBuffer.y = 0.0f;
    aBuffer.w = 0.0f;
    aBuffer.h = 0.0f;
    return;
  }

  glm::vec2 maximum(mGeometry[0].Location[0], mGeometry[0].Location[1]);
  glm::vec2 minimum(mGeometry[0].Location[0], mGeometry[0].Location[1]);

  for (auto it = mGeometry.begin() + 1; it != mGeometry.end(); ++it)
    for (int j = 0; j < 2; ++j) {
      maximum[j] = std::max(maximum[j], it->Location[j]);
      minimum[j] = std::min(minimum[j], it->Location[j]);
    }

  aBuffer.x = minimum.x;
  aBuffer.y = minimum.y;
  aBuffer.w = maximum.x - minimum.x;
  aBuffer.h = maximum.y - minimum.y;
}

//===========================================================================//
//=== GroupNode =============================================================//
//===========================================================================//

const int GroupNode::TypeId = AllocType();

GroupNode::GroupNode()
  : RenderNode(TypeId)
{}

const decltype(GroupNode::mChildren)&
GroupNode::GetChildren() const
{
  return mChildren;
}

void
GroupNode::AddChild(RenderNode& aNode)
{
  mChildren.emplace_back(aNode);
  Adopt(aNode);
  DirtyBounds();
}

void
GroupNode::RemoveChild(RenderNode& aNode)
{
  auto it = std::find_if(
    //
    mChildren.begin(),
    mChildren.end(),
    [&](std::reference_wrapper<RenderNode> elem) {
      return &aNode == &elem.get();
    });

  assert(it != mChildren.end());
  mChildren.erase(it);
  Disown(aNode);
  DirtyBounds();
}

void
GroupNode::ImplLocalBounds(SDL_FRect& aBuffer) const
{
  aBuffer.x = 0.0f;
  aBuffer.y = 0.0f;
  aBuffer.w = 0.0f;
  aBuffer.h = 0.0f;

  for (std::reference_wrapper<RenderNode> child : mChildren)
    SDL_UnionRect(&child.get().GetLocalBounds(), &aBuffer, &aBuffer);
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

void
TextNode::ImplLocalBounds(SDL_FRect& aBuffer) const
{
  aBuffer.x = 0.0f;
  aBuffer.y = 0.0f;
  aBuffer.w = 1.0f;
  aBuffer.h = 1.0f;
}

//===========================================================================//
//=== Traversal =============================================================//
//===========================================================================//

namespace {

void
CleanState(const RenderNode& aNode, int& aLayer)
{
  glMatrixMode(GL_MODELVIEW);

  // Additional special states for some nodes.
  if (aNode.GetType() == ClipNode::TypeId) {
    auto& ref = static_cast<const ClipNode&>(aNode);
    SDL_FRect bounds = ref.GetClipRect();

    // Decrement the stencil buffer in the region.
    glPushAttrib(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    glPushMatrix();
    glTranslatef(bounds.x, bounds.y, 0.0f);
    glScalef(bounds.w, bounds.h, 1.0f);

    glBegin(GL_POLYGON);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(1.0f, 0.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();

    glPopAttrib();
    glPopMatrix();

    // Test the stencil buffer against the lower number.
    // This old stencil function should be on the attribute stack.
    glStencilFunc(GL_EQUAL, --aLayer, 0xFF);
  }

  glPopMatrix();
}

void
MergeState(const RenderNode& aNode, int& aLayer)
{
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();

  // All nodes come with transformations to apply.
  const glm::vec2& translate = aNode.GetTranslate();
  glTranslatef(translate.x, translate.y, 0.0f);
  const glm::vec2& scale = aNode.GetScale();
  glScalef(scale.x, scale.y, 0.0f);

  // Additional special states for some nodes.
  if (aNode.GetType() == ClipNode::TypeId) {
    auto& ref = static_cast<const ClipNode&>(aNode);
    SDL_FRect bounds = ref.GetClipRect();

    // Increment the stencil buffer in the region.
    glPushAttrib(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    glPushMatrix();
    glTranslatef(bounds.x, bounds.y, 0.0f);
    glScalef(bounds.w, bounds.h, 1.0f);

    glBegin(GL_POLYGON);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(1.0f, 0.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();

    glPopAttrib();
    glPopMatrix();

    // Test the stencil buffer against the layer number.
    glStencilFunc(GL_EQUAL, ++aLayer, 0xFF);
    glEnable(GL_STENCIL_TEST);
  }
}

void
VisitState(const RenderNode& aNode)
{
  if (aNode.GetType() == GeomNode::TypeId) {
    auto& ref = static_cast<const GeomNode&>(aNode);

    if (ref.GetGeometry().empty())
      return;

    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT);

    if (ref.GetTexture()) {
      glEnable(GL_TEXTURE_2D);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
      glBindTexture(GL_TEXTURE_2D, *ref.GetTexture());
    }

    glBegin(ref.GetDrawMode());

    for (const GeomNode::Vertex& elem : ref.GetGeometry()) {
      glColor4fv(glm::value_ptr(elem.Color));
      glTexCoord2fv(glm::value_ptr(elem.TexCoord));
      glVertex2fv(glm::value_ptr(elem.Location));
    }

    glEnd();
    glPopAttrib();
  } else if (aNode.GetType() == TextNode::TypeId) {
    auto& ref = static_cast<const TextNode&>(aNode);

    if (!ref.GetTexture())
      return;

    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT);
    glEnable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, *ref.GetTexture());

    glBegin(GL_POLYGON);
    glColor4fv(glm::value_ptr(ref.GetColor()));
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

void
Traverse(const RenderNode& aNode, int& aLayer, bool aBoundingBox)
{
  SDL_FRect bounds = aNode.GetLocalBounds();
  glm::mat4 modelView;
  glGetFloatv(GL_MODELVIEW_MATRIX, glm::value_ptr(modelView));
  bounds = TransformBounds(bounds, modelView);

  {
    SDL_FRect view{ -1.0f, -1.0f, 2.0f, 2.0f };

    if (SDL_HasIntersection(&bounds, &view))
      aNode.Visited();
    else
      return;
  }

  MergeState(aNode, aLayer);
  VisitState(aNode);

  if (aNode.GetType() == ClipNode::TypeId) {
    auto& ref = static_cast<const ClipNode&>(aNode);
    if (ref.GetChild() != nullptr)
      Traverse(*ref.GetChild(), aLayer, aBoundingBox);
  } else if (aNode.GetType() == GroupNode::TypeId) {
    auto& ref = static_cast<const GroupNode&>(aNode);
    for (auto ref : ref.GetChildren())
      Traverse(ref.get(), aLayer, aBoundingBox);
  }

  CleanState(aNode, aLayer);

  if (aBoundingBox) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_TEXTURE_2D);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glTranslatef(bounds.x, bounds.y, 0.0f);
    glScalef(bounds.w, bounds.h, 1.0f);

    glBegin(GL_LINE_LOOP);
    glColor4f(1.0f, 0.2f, 0.2f, 1.0f);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(1.0f, 0.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(0.0f, 1.0f);
    glEnd();

    glPopAttrib();
    glPopMatrix();
  }
}

}

void
Render(const RenderNode& aRoot, bool aBoundingBox)
{
  std::vector<std::reference_wrapper<const RenderNode>> stack;
  int layer = 0;

  // Load the stack with sequence of parent nodes with root as last entry.
  for (auto cursor = aRoot.GetParent(); cursor; cursor = cursor->GetParent())
    stack.emplace_back(*cursor);

  // Go down the tree and apply the render states in-order.
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    MergeState(it->get(), layer);

  // Invoke the normal recursive rendering logic.
  Traverse(aRoot, layer, aBoundingBox);

  // Clean up the render state in the reverse order.
  for (auto it = stack.begin(); it != stack.end(); ++it)
    CleanState(it->get(), layer);
}
