#include "Viewer.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <GL/glew.h>
#include <sigc++/sigc++.h>

#include "Graphics.hpp"
#include "Helper.hpp"
#include "Worker.hpp"

namespace {

class TextWidget
{
  TextNode mRootNode;
  Texture mTexture;
  SDL_FRect mBounds;

public:
  TextWidget() { mRootNode.SetTexture(&mTexture); }

  explicit TextWidget(const char* aText)
    : TextWidget()
  {
    SetText(aText);
  }

  const RenderNode& GetNode() const { return mRootNode; }
  RenderNode& GetNode() { return mRootNode; }

  void Layout(const SDL_FRect& aBounds)
  {
    mBounds = aBounds;
    mRootNode.SetScale({ aBounds.h * mTexture.GetAspectRatio(), aBounds.h });
    mRootNode.SetTranslate({ aBounds.x, aBounds.y });
  }

  void SetText(const char* aText)
  {
    mTexture.StrokeText(aText);
    Layout(mBounds);
  }
};

class TileWidget
{
  /// Contains all the options for aspect ratios.
  ApiFuzzyTile mModel;

  /// Used for sizing and drawing the texture to the screen.
  GeomNode mRootNode;
  Texture mTexture;

  /// Points to the currently-selected aspect ratio.
  decltype(mModel.TileImages)::iterator mImageSelection;
  std::optional<AsyncImage> mImageQuery;
  sigc::connection mImageTrigger;

public:
  /// Emitted whenever the texture aspect ratio is altered.
  mutable sigc::signal<void(float)> AspectRatioChanged;

  explicit TileWidget(ApiFuzzyTile aModel)
    : mModel(std::move(aModel))
    , mImageSelection(mModel.TileImages.end())
  {
    mRootNode.SetTexture(&mTexture);
    RequestAspectRatio(1.0f);
  }

  float GetImageAspectRatio() const
  {
    float ratio = mTexture.GetAspectRatio();

    if (ratio == 0.0f) {
      SDL_FRect bounds = mRootNode.GetLocalBounds();
      return bounds.w / bounds.h;
    } else {
      return ratio;
    }
  }

  const RenderNode& GetNode() const { return mRootNode; }
  RenderNode& GetNode() { return mRootNode; }

  void Layout(const SDL_FRect& aBounds)
  {
    mRootNode.SetScale({ aBounds.w, aBounds.h });
    mRootNode.SetTranslate({ aBounds.x, aBounds.y });
  }

  void RequestAspectRatio(float aRatio)
  {
    auto it = std::min_element(
      // Find the closest aspect ratio.
      mModel.TileImages.begin(),
      mModel.TileImages.end(),
      [&](const ApiImage& a, const ApiImage& b) {
        return abs(a.AspectRatio - aRatio) < abs(b.AspectRatio - aRatio);
      });

    if (it == mImageSelection)
      return;

    mImageSelection = it;
    mImageTrigger.disconnect();
    mImageQuery.reset();

    mImageTrigger = mRootNode.Visited.connect(
      // Only download when this is not clipped out.
      [&]() {
        mImageTrigger.disconnect();
        OnVisited();
      });
  }

  void SetDepth(float aNewValue)
  {
    for (GeomNode::Vertex& vert : mRootNode.GetGeometry())
      vert.Location.z = aNewValue;
  }

private:
  void OnVisited()
  {
    if (mImageSelection == mModel.TileImages.end())
      return;

    mImageQuery.emplace(mImageSelection->ResourceLink);

    mImageQuery->Failed.connect(
      //
      [&](std::string aMessage) {
        SDL_LogWarn(0, "%s", aMessage.c_str());
        mImageQuery.reset();
      });
    mImageQuery->Finished.connect(
      //
      [&](std::shared_ptr<SDL_Surface> aSurface) {
        mImageQuery.reset();
        mTexture.LoadImage(*aSurface);
        AspectRatioChanged(mTexture.GetAspectRatio());
      });

    mImageQuery->Enqueue();
  }
};

constexpr float Margin = 0.025;
constexpr float Spacing = 0.015;

class RowWidget
{
  ApiSetRef mRefModel;
  GroupNode mRootNode;
  TextWidget mTitle;
  SDL_FRect mBounds;

  std::optional<AsyncQuery> mQuery;
  sigc::connection mQueryTrigger;

  float mWindowStart;
  std::optional<int> mSelection;
  std::vector<std::unique_ptr<TileWidget>> mTiles;

public:
  explicit RowWidget(ApiFuzzySet aModel)
    : RowWidget()
  {
    OnQueryFinished(std::move(aModel));
  }

  explicit RowWidget(ApiSetRef aModel)
    : RowWidget()
  {
    mRefModel = std::move(aModel);
    mTitle.SetText(mRefModel.Text.FullTitle.c_str());

    mQueryTrigger = mRootNode.Visited.connect(
      // Only download when this is not clipped out.
      [&]() {
        mQueryTrigger.disconnect();
        OnVisited();
      });
  }

  const RenderNode& GetNode() const { return mRootNode; }
  RenderNode& GetNode() { return mRootNode; }
  decltype(mTiles)::size_type GetCount() const { return mTiles.size(); }

  void Layout(const SDL_FRect& aBounds)
  {
    mBounds = aBounds;
    float x = 0.0f;
    float y = 0.0f;
    float h = aBounds.h;

    // Text nodes choose their own width.
    mTitle.Layout({ x, y, 0, 0.03f });
    y += 0.03f + Spacing;
    h -= 0.03f + Spacing;

    for (auto it = mTiles.begin(); it != mTiles.end(); ++it) {
      float ratio = it->get()->GetImageAspectRatio();
      SDL_FRect bounds{ x, y, h * ratio, h };
      x += bounds.w + Spacing;

      if (mSelection && it - mTiles.begin() == mSelection.value()) {
        // Slide the window so this tile is always visible.
        if (bounds.x + bounds.w > mWindowStart + aBounds.w)
          mWindowStart = bounds.x + bounds.w - aBounds.w;
        else if (bounds.x < mWindowStart)
          mWindowStart = bounds.x;

        // This can happen after window calculations.
        float xm = (bounds.x + bounds.x + bounds.w) / 2.0f;
        float ym = (bounds.y + bounds.y + bounds.h) / 2.0f;
        constexpr float coeff = 1.3f;
        bounds.x = xm + (bounds.x - xm) * coeff;
        bounds.y = ym + (bounds.y - ym) * coeff;
        bounds.w *= coeff;
        bounds.h *= coeff;

        // Hack for depth testing.
        it->get()->SetDepth(-1.0f);
      } else {
        // Hack for depth testing.
        it->get()->SetDepth(+0.0f);
      }

      it->get()->Layout(bounds);
    }

    mRootNode.SetTranslate({ aBounds.x - mWindowStart, aBounds.y });
  }

  void RequestAspectRatio(float aRatio)
  {
    for (std::unique_ptr<TileWidget>& tile : mTiles)
      tile->RequestAspectRatio(aRatio);
  }

  void Select(std::optional<int> aNewValue)
  {
    if (mSelection != aNewValue) {
      mSelection = aNewValue;
      Layout(mBounds);
    }
  }

private:
  RowWidget()
    : mWindowStart(0.0f)
  {
    mRootNode.AddChild(mTitle.GetNode());
  }

  void OnQueryFinished(ApiFuzzySet aModel)
  {
    mTitle.SetText(aModel.Text.FullTitle.c_str());

    for (ApiFuzzyTile& ent : aModel.Tiles) {
      mTiles.emplace_back(new TileWidget(std::move(ent)));
      mRootNode.AddChild(mTiles.back()->GetNode());
      mTiles.back()->AspectRatioChanged.connect(
        [&](float) { Layout(mBounds); });
    }

    Layout(mBounds);
  }

  void OnVisited()
  {
    std::ostringstream oss;
    oss << "https://cd-static.bamgrid.com/dp-117731241344/sets";
    oss << '/' << mRefModel.ReferenceId << ".json";

    mQuery.emplace(oss.str());

    mQuery->Failed.connect(
      //
      [&](std::string aMessage) { SDL_LogWarn(0, "%s", aMessage.c_str()); });
    mQuery->Finished.connect(
      //
      [&](std::shared_ptr<AsyncQuery::ResultType> aResult) {
        OnQueryFinished(std::move(std::get<ApiFuzzySet>(*aResult)));
        mQuery.reset();
      });

    mQuery->Enqueue(AsyncQuery::Dereference);
  }
};

class HomeWidget
{
  GroupNode mRootNode;
  ClipNode mContentClip;
  GroupNode mContentNode;
  SDL_FRect mBounds;

  TextWidget mTitle;

  std::optional<AsyncQuery> mQuery;

  float mWindowStart;
  std::optional<int> mSelectRow;
  std::vector<int> mSelectColumn;
  std::vector<std::unique_ptr<RowWidget>> mRows;

public:
  HomeWidget()
    : mWindowStart(0.0f)
  {
    mRootNode.AddChild(mContentClip);
    mRootNode.AddChild(mTitle.GetNode());
    mContentClip.SetChild(&mContentNode);
    mTitle.SetText("Loading...");

    // Download the home screen and deal with it later.
    mQuery.emplace("https://cd-static.bamgrid.com/dp-117731241344/home.json");
    mQuery->Failed.connect(
      // Print error to the console and ignore.
      [](std::string aMessage) { SDL_LogWarn(0, "%s", aMessage.c_str()); });
    mQuery->Finished.connect(
      // Populate the interface when the model data is ready.
      [&](auto ptr) {
        OnQueryFinished(std::move(std::get<ApiHome>(*ptr)));
        mQuery.reset();
      });
    mQuery->Enqueue(AsyncQuery::Home);
  }

  const RenderNode& GetNode() const { return mRootNode; }
  RenderNode& GetNode() { return mRootNode; }

  void Event(const SDL_KeyboardEvent& aEvent)
  {
    if (aEvent.type != SDL_KEYDOWN)
      return;
    else if (mRows.empty())
      return;

    if (mSelectRow) {
      if (aEvent.keysym.sym == SDLK_LEFT) {
        int row = mSelectRow.value();
        int max = mRows[row]->GetCount();
        int col = std::clamp(mSelectColumn[row] - 1, 0, max - 1);
        mRows[row]->Select(col);
        mSelectColumn[row] = col;
      } else if (aEvent.keysym.sym == SDLK_RIGHT) {
        int row = mSelectRow.value();
        int max = mRows[row]->GetCount();
        int col = std::clamp(mSelectColumn[row] + 1, 0, max - 1);
        mRows[row]->Select(col);
        mSelectColumn[row] = col;
      } else if (aEvent.keysym.sym == SDLK_DOWN) {
        int idx = mSelectRow.value();
        mRows[idx]->Select(std::nullopt);
        idx = std::clamp(idx + 1, 0, static_cast<int>(mRows.size()) - 1);
        mRows[idx]->Select(mSelectColumn[idx]);
        mSelectRow = idx;
      } else if (aEvent.keysym.sym == SDLK_UP) {
        int idx = mSelectRow.value();
        mRows[idx]->Select(std::nullopt);
        idx = std::clamp(idx - 1, 0, static_cast<int>(mRows.size()) - 1);
        mRows[idx]->Select(mSelectColumn[idx]);
        mSelectRow = idx;
      }

      Layout(mBounds);
    } else {
      mSelectRow.emplace(0);
      mRows[0]->Select(0);
    }
  }

  void Layout(const SDL_FRect& aBounds)
  {
    mBounds = aBounds;
    float x = Margin;
    float y = Margin;
    float w = aBounds.w - Margin - Margin;
    float h = aBounds.h - Margin - Margin;

    // Text nodes choose their own width.
    mTitle.Layout({ x, y, 0, 0.05f });
    y += 0.05f + Spacing;
    h -= 0.05f + Spacing;

    mContentClip.SetTranslate({ x, y });
    {
      // I spend eight hours on clip node only to not use it!
      mContentClip.SetClipRect({ -10.0f, -10.0f, +20.0f, +20.0f });
      // mContentClip.SetClipRect({ 0.0f, 0.0f, w, h });
    }
    x = 0.0f; // move inside of the clip node
    y = 0.0f; // move inside of the clip node
    w -= Spacing + Spacing;
    h -= Spacing + Spacing;

    //
    for (auto it = mRows.begin(); it != mRows.end(); ++it) {
      SDL_FRect bounds{ x, y, w, 0.15f };
      it->get()->Layout(bounds);
      it->get()->RequestAspectRatio(aBounds.w / aBounds.h);

      if (mSelectRow && it - mRows.begin() == mSelectRow.value()) {
        // Slide the window so this row is always visible.
        if (bounds.y + bounds.h > mWindowStart + h)
          mWindowStart = bounds.y + bounds.h - h;
        else if (bounds.y < mWindowStart)
          mWindowStart = bounds.y;
      }

      y += bounds.h + Spacing;
    }

    mRootNode.SetTranslate({ 0.0f, -mWindowStart });
  }

private:
  void OnQueryFinished(ApiHome aModel)
  {
    mTitle.SetText(aModel.Text.FullTitle.c_str());

    for (auto& variant : aModel.Containers) {
      std::visit(
        [&](auto&& ref) {
          using T = std::decay_t<decltype(ref)>;
          mRows.emplace_back(new RowWidget(std::forward<T>(ref)));
        },
        std::move(variant));

      mContentNode.AddChild(mRows.back()->GetNode());
    }

    mSelectRow.reset();
    mSelectColumn.resize(mRows.size(), 0);

    Layout(mBounds);
  }
};

}

class Viewer::Private
{
  HomeWidget mHome;

  float mViewportWidth;
  float mViewportHeight;

public:
  Private()
  {
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    OnResize(viewport[2], viewport[3]);
  }

  Private(const Private& aOther) = delete;
  Private& operator=(const Private& aOther) = delete;

  void DrawFrame()
  {
    glClearStencil(0);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Change to regular GUI coordinate system.
    glTranslatef(-1.0f, +1.0f, +0.0f);
    glScalef(+2.0f, -2.0f, +1.0f);

    // Preserve the aspect ratio for all nodes.
    {
      float aspect = mViewportWidth / mViewportHeight;
      if (mViewportWidth > mViewportHeight)
        glScalef(1.0f / aspect, 1.0f, 1.0f);
      else
        glScalef(1.0f, aspect, 1.0f);
    }

    Render(mHome.GetNode(), false);
  }

  void Event(const SDL_Event& aEvent)
  {
    switch (aEvent.type) {
      case SDL_KEYDOWN:
      case SDL_KEYUP:
        mHome.Event(aEvent.key);
        break;
      case SDL_WINDOWEVENT:
        Event(aEvent.window);
        break;
    }
  }

private:
  void Event(const SDL_WindowEvent& aEvent)
  {
    if (aEvent.event == SDL_WINDOWEVENT_RESIZED ||
        aEvent.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
      OnResize(aEvent.data1, aEvent.data2);
    }
  }

  void OnResize(int aWidth, int aHeight)
  {
    glViewport(0, 0, aWidth, aHeight);
    mViewportWidth = aWidth;
    mViewportHeight = aHeight;

    float aspect = mViewportWidth / mViewportHeight;
    if (mViewportWidth > mViewportHeight)
      mHome.Layout({ 0.0f, 0.0f, aspect, 1.0f });
    else
      mHome.Layout({ 0.0f, 0.0f, 1.0f, 1.0f / aspect });
  }
};

Viewer::Viewer()
  : mPrivate(new Private)
{}

Viewer::~Viewer() = default;

void
Viewer::DrawFrame()
{
  mPrivate->DrawFrame();
}

void
Viewer::Event(const SDL_Event& aEvent)
{
  mPrivate->Event(aEvent);
}
