#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#include <optional>

#include <GL/glew.h>
#include <SDL.h>
#include <glm/glm.hpp>
#include <sigc++/sigc++.h>

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

class RenderNode
{
  int mType;
  /// Optional link to parent node in tree.
  RenderNode* mParentNode;

  glm::vec2 mScale;
  glm::vec2 mTranslate;

  mutable std::optional<SDL_FRect> mLocalBounds;

public:
  mutable sigc::signal<void()> Visited;

  RenderNode(const RenderNode& aOther) = delete;
  RenderNode& operator=(const RenderNode& aOther) = delete;
  virtual ~RenderNode() = default;

  int GetType() const;

  const RenderNode* GetParent() const;
  RenderNode* GetParent();

  const glm::vec2& GetScale() const;
  const glm::vec2& GetTranslate() const;
  void SetScale(const glm::vec2& aNewValue);
  void SetTranslate(const glm::vec2& aNewValue);

  const SDL_FRect& GetLocalBounds() const;

protected:
  static int AllocType();

  explicit RenderNode(int aType);

  void Adopt(RenderNode& aOther);
  void Disown(RenderNode& aOther);
  void DirtyBounds();
  /// Does not include the base-class transformations.
  virtual void ImplLocalBounds(SDL_FRect& aBuffer) const = 0;
};

class ClipNode : public RenderNode
{
  RenderNode* mChildNode;
  SDL_FRect mClipRect;

public:
  static const int TypeId;

  ClipNode();

  const RenderNode* GetChild() const;
  void SetChild(RenderNode* aNewValue = nullptr);

  const SDL_FRect& GetClipRect() const;
  void SetClipRect(const SDL_FRect& aNewValue);

protected:
  /// Does not include the base-class transformations.
  void ImplLocalBounds(SDL_FRect& aBuffer) const override;
};

class GeomNode : public RenderNode
{
  GLenum mDrawMode;
  Texture* mTexture;

public:
  struct Vertex
  {
    glm::vec4 Color;
    glm::vec2 Location;
    glm::vec2 TexCoord;
  };

  std::vector<Vertex> mGeometry;

public:
  static const int TypeId;

  GeomNode();

  GLenum GetDrawMode() const;
  void SetDrawMode(GLenum aNewValue);

  const Texture* GetTexture() const;
  Texture* GetTexture();
  void SetTexture(Texture* aNewValue = nullptr);

  using RenderNode::DirtyBounds;
  const std::vector<Vertex>& GetGeometry() const;
  std::vector<Vertex>& GetGeometry();

private:
  /// Does not include the base-class transformations.
  void ImplLocalBounds(SDL_FRect& aBuffer) const override;
};

class GroupNode : public RenderNode
{
  std::vector<std::reference_wrapper<RenderNode>> mChildren;

public:
  static const int TypeId;

  GroupNode();

  const decltype(mChildren)& GetChildren() const;
  void AddChild(RenderNode& aNode);
  void RemoveChild(RenderNode& aNode);

private:
  /// Does not include the base-class transformations.
  void ImplLocalBounds(SDL_FRect& aBuffer) const override;
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
  /// Does not include the base-class transformations.
  void ImplLocalBounds(SDL_FRect& aBuffer) const override;
};

void
Render(const RenderNode& aRoot, bool aBoundingBox = true);

void
InitGraphics();
void
FreeGraphics();

#endif
