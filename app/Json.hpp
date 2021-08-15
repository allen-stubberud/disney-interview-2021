#ifndef JSON_HPP
#define JSON_HPP

/**
 * \file
 * \brief Decode the JSON web API.
 */

#include <cstddef>
#include <istream>
#include <string>
#include <variant>
#include <vector>

struct ApiCollection;

/// Single image entry for the tiles.
struct ApiImage
{
  /// Image width divided by height.
  float AspectRatio;
  /// Dimensions of some original image file.
  size_t MasterWidth;
  /// Dimensions of some original image file.
  size_t MasterHeight;
  /// HTTP link to download the JPEG.
  std::string ResourceLink;
};

/// Rough translation of the text structure.
struct ApiFuzzyText
{
  /// Human-readable title text; might be empty.
  std::string FullTitle;
  /// Name for internal use; might be empty.
  std::string SlugTitle;
};

/**
 * \brief Rough translation of several types of tiles.
 *
 * There are several types of objects, each with different types in the JSON
 * code, that correspond to tiles shown on the screen. These include:
 * - DmcSeries
 * - DmcVideo
 * - StandardCollection
 *
 * This structure stores the useful common subset of these.
 */
struct ApiFuzzyTile
{
  /// Label for the tile; probably does not have slug.
  ApiFuzzyText Text;
  /// Available aspect ratios for the tile image.
  std::vector<ApiImage> TileImages;
};

/// Rough translation of linear tile collections.
struct ApiFuzzySet
{
  /// Name is usually displayed above the row on the screen.
  ApiFuzzyText Text;
  /// Displayed as single images but have different meanings.
  std::vector<ApiFuzzyTile> Tiles;
};

/// Rough translation of references to remote sets.
struct ApiFuzzySetRef
{
  /// Do not use this; there are bugs in the data itself.
  ApiFuzzyText Text;
  /// Used to compute the URL for the JSON file.
  std::string ReferenceId;
  /// Type of structure in the referenced JSON file.
  std::string ReferenceType;
};

/// Rough translation of the top-level standard collection structure.
struct ApiHome
{
  /// Name of the home screen.
  ApiFuzzyText Text;
  /// Only some of the rows are provided up-front.
  std::vector<std::variant<ApiFuzzySet, ApiFuzzySetRef>> Containers;
};

/// Read the remote sets referenced by the home screen.
ApiFuzzySet
ApiReadRef(std::istream& aInput);

/// Read the home screen API.
ApiHome
ApiReadHome(std::istream& aInput);

#endif
