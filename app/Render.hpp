#ifndef RENDER_HPP
#define RENDER_HPP

/**
 * \file
 * \brief OpenGL rendering and transformation tree.
 */

#include <functional>
#include <vector>

#include <GL/glew.h>
#include <SDL.h>
#include <glm/glm.hpp>

#include "Signal.hpp"

class Texture
{
  GLuint mHandle;
  float mAspectRatio;
  unsigned mWidth;
  unsigned mHeight;

public:
  Texture();
  explicit Texture(const char* aText);
  explicit Texture(const SDL_Surface& aBuffer);
  Texture(Texture&& aOther) noexcept;
  Texture& operator=(Texture&& aOther) noexcept;
  ~Texture();

  operator GLuint() const;

  float GetAspectRatio() const;
  unsigned GetWidth() const;
  unsigned GetHeight() const;

  void LoadImage(const SDL_Surface& aImage);
  void StrokeText(const char* aText);
};

struct RenderOptions
{
  bool BoundingBoxEnable;
  glm::vec4 BoundingBoxColor;

  RenderOptions();
};

class RenderNode
{
  int mType;
  RenderNode* mParent;
  glm::mat4 mLocalTransform;
  mutable std::optional<SDL_FRect> mLocalBounds;

public:
  mutable Signal<> Visited;

  RenderNode(const RenderNode& aOther) = delete;
  RenderNode& operator=(const RenderNode& aOther) = delete;
  virtual ~RenderNode() = default;

  int GetType() const;

  const RenderNode* GetParent() const;
  RenderNode* GetParent();

  const SDL_FRect& GetLocalBounds() const;
  const glm::mat4& GetLocalTransform() const;
  void SetLocalTransform(const glm::mat4& aNewValue);

  void Draw(const RenderOptions& aOptions);
  void Draw(const glm::mat4& aModelView, const RenderOptions& aOptions);

protected:
  static int AllocType();

  explicit RenderNode(int aType);

  void Adopt(RenderNode& aOther);
  void Disown(RenderNode& aOther);
  void DirtyBounds();

  virtual SDL_FRect ImplLocalBounds() const = 0;

  virtual void ImplDraw(const glm::mat4& aModelView,
                        const RenderOptions& aOptions) const = 0;
};

struct Vertex
{
  glm::vec4 Color;
  glm::vec2 Location;
  glm::vec2 TexCoord;
};

class GeomNode : public RenderNode
{
  GLenum mDrawMode;
  Texture* mTexture;

  bool mGeometryLock;
  std::vector<Vertex> mGeometry;

public:
  static const int TypeId;

  GeomNode();

  GLenum GetDrawMode() const;
  void SetDrawMode(GLenum aNewValue);

  const Texture* GetTexture() const;
  Texture* GetTexture();
  void SetTexture(Texture* aNewValue = nullptr);

  const std::vector<Vertex>& GetGeometry() const;
  std::vector<Vertex>& LockGeometry();
  void UnlockGeometry();

private:
  SDL_FRect ImplLocalBounds() const override;

  void ImplDraw(const glm::mat4& aModelView,
                const RenderOptions& aOptions) const override;
};

class GroupNode : public RenderNode
{
  bool mChildrenLock;
  std::vector<std::reference_wrapper<RenderNode>> mChildren;

public:
  static const int TypeId;

  GroupNode();

  const decltype(mChildren)& GetChildren() const;
  decltype(mChildren)& LockChildren();
  void UnlockChildren();

private:
  SDL_FRect ImplLocalBounds() const override;

  void ImplDraw(const glm::mat4& aModelView,
                const RenderOptions& aOptions) const override;
};

class TextNode : public RenderNode
{
  glm::vec4 mColor;
  Texture* mTexture;

public:
  static const int TypeId;

  TextNode();

  const glm::vec4& GetColor() const;
  void SetColor(const glm::vec4& aNewValue);

  const Texture* GetTexture() const;
  Texture* GetTexture();
  void SetTexture(Texture* aNewValue = nullptr);

private:
  SDL_FRect ImplLocalBounds() const override;

  void ImplDraw(const glm::mat4& aModelView,
                const RenderOptions& aOptions) const override;
};

SDL_FRect
TransformBounds(const SDL_FRect& aBounds, const glm::mat4& aTransform);

#endif
