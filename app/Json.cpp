#include "Json.hpp"

#include <cstddef>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/memorystream.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>

#define RESOURCE(x)                                                            \
  extern const unsigned char x##_data[];                                       \
  extern const size_t x##_size;

RESOURCE(common_schema_json)
RESOURCE(home_schema_json)
RESOURCE(meta_schema_json)
RESOURCE(ref_schema_json)

namespace {

template<typename T>
rapidjson::Document
ReadJsonDocument(T&& aWrapper)
{
  rapidjson::Document result;

  if (result.ParseStream(aWrapper).HasParseError()) {
    std::ostringstream oss;
    oss << "JSON parsing error: ";
    oss << rapidjson::GetParseError_En(result.GetParseError());
    throw std::runtime_error(oss.str());
  }

  return result;
}

void
ValidateJsonDocument(const rapidjson::Value& aDocument,
                     const rapidjson::SchemaDocument& aSchema)
{
  rapidjson::SchemaValidator validator(aSchema);

  if (!aDocument.Accept(validator)) {
    std::ostringstream oss;
    oss << "JSON validation error:" << std::endl;

    // Get the location of the error in the document.
    rapidjson::StringBuffer buf;
    validator.GetInvalidDocumentPointer().StringifyUriFragment(buf);
    oss << '\t' << "Document pointer: " << buf.GetString() << std::endl;

    // Find out why there was an error.
    buf.Clear();
    validator.GetInvalidSchemaPointer().StringifyUriFragment(buf);
    oss << '\t' << "Schema pointer: " << buf.GetString() << std::endl;
    oss << '\t' << "Schema keyword: " << validator.GetInvalidSchemaKeyword();

    throw std::runtime_error(oss.str());
  }
}

class RemoteSchemaProvider : public rapidjson::IRemoteSchemaDocumentProvider
{
  std::optional<rapidjson::SchemaDocument> mMetaSchema;
  std::unordered_map<std::string, rapidjson::SchemaDocument> mTable;

public:
  RemoteSchemaProvider()
  {
    rapidjson::MemoryStream wrapper(
      reinterpret_cast<const char*>(meta_schema_json_data),
      meta_schema_json_size);

    rapidjson::Document dom = ReadJsonDocument(wrapper);
    mMetaSchema.emplace(dom);
    ValidateJsonDocument(dom, mMetaSchema.value());
  }

  const rapidjson::SchemaDocument* GetRemoteDocument(
    const char* aLink,
    rapidjson::SizeType aLinkLength) override
  {
    (void)aLinkLength;
    std::string key;

    // Compute the key by cutting at the pount sign.
    {
      std::string_view search(aLink);
      size_t first = search.find_first_of('#');

      if (first == decltype(search)::npos)
        key = search;
      else
        key = search.substr(0, first);
    }

    // Search the table for the schema.
    decltype(mTable)::iterator iter = mTable.find(key);

    // Insert new elements into the table if required.
    if (iter == mTable.end()) {
      const static struct
      {
        const char* Key;
        const char* Data;
        size_t Length;
      } ResourceTable[] = {
        {
          "common-schema.json",
          reinterpret_cast<const char*>(common_schema_json_data),
          common_schema_json_size,
        },
        {
          "home-schema.json",
          reinterpret_cast<const char*>(home_schema_json_data),
          home_schema_json_size,
        },
        {
          "ref-schema.json",
          reinterpret_cast<const char*>(ref_schema_json_data),
          ref_schema_json_size,
        },
      };

      for (auto ent : ResourceTable)
        if (ent.Key == key) {
          rapidjson::MemoryStream wrapper(ent.Data, ent.Length);
          rapidjson::Document dom = ReadJsonDocument(wrapper);
          ValidateJsonDocument(dom, mMetaSchema.value());

          // Make sure to use 'this' to resolve other schemas.
          iter = mTable
                   .emplace(std::piecewise_construct,
                            std::forward_as_tuple(ent.Key),
                            std::forward_as_tuple(dom, this))
                   .first;

          break;
        }
    }

    return (iter == mTable.end()) ? nullptr : &iter->second;
  }
};

RemoteSchemaProvider gProvider;

ApiImage
ReadImage(const rapidjson::Value& aKey, const rapidjson::Value& aValue)
{
  ApiImage result;
  result.AspectRatio = atof(aKey.GetString());

  // Bug in RapidJSON means only valid aspect ratios are validated.
  if (result.AspectRatio > 0) {
    auto first = aValue.MemberBegin();
    const rapidjson::Value& table = first->value["default"];
    result.MasterWidth = table["masterWidth"].GetInt();
    result.MasterHeight = table["masterHeight"].GetInt();
    result.ResourceLink = table["url"].GetString();
  }

  return result;
}

ApiFuzzyText
ReadFuzzyText(const rapidjson::Value& aValue)
{
  ApiFuzzyText result;

  {
    const rapidjson::Value& title = aValue["title"];
    auto full = title.FindMember("full");
    auto slug = title.FindMember("slug");

    if (full != title.MemberEnd()) {
      const rapidjson::Value& entry =
        full->value.MemberBegin()->value["default"];

      result.FullTitle = entry["content"].GetString();
    }

    if (slug != title.MemberEnd()) {
      const rapidjson::Value& entry =
        slug->value.MemberBegin()->value["default"];

      result.SlugTitle = entry["content"].GetString();
    }
  }

  return result;
}

ApiFuzzyTile
ReadFuzzyTile(const rapidjson::Value& aValue)
{
  ApiFuzzyTile result;
  result.Text = ReadFuzzyText(aValue["text"]);

  {
    const rapidjson::Value& table = aValue["image"];
    auto tile = table.FindMember("tile");

    if (tile != table.MemberEnd())
      for (const auto& ent : tile->value.GetObject())
        result.TileImages.emplace_back(ReadImage(ent.name, ent.value));
  }

  return result;
}

ApiFuzzySet
ReadFuzzySet(const rapidjson::Value& aValue)
{
  ApiFuzzySet result;
  result.Text = ReadFuzzyText(aValue["text"]);

  for (const auto& ent : aValue["items"].GetArray())
    result.Tiles.emplace_back(ReadFuzzyTile(ent));

  return result;
}

ApiFuzzySetRef
ReadFuzzySetRef(const rapidjson::Value& aValue)
{
  ApiFuzzySetRef result;
  result.Text = ReadFuzzyText(aValue["text"]);
  result.ReferenceId = aValue["refId"].GetString();
  result.ReferenceType = aValue["refType"].GetString();
  return result;
}

}

ApiFuzzySet
ApiReadRef(std::istream& aInput)
{
  const auto& schema = *gProvider.GetRemoteDocument("ref-schema.json", 0);
  rapidjson::IStreamWrapper wrapper(aInput);
  rapidjson::Document dom = ReadJsonDocument(wrapper);
  ValidateJsonDocument(dom, schema);
  return ReadFuzzySet(dom["data"].MemberBegin()->value);
}

ApiHome
ApiReadHome(std::istream& aInput)
{
  const auto& schema = *gProvider.GetRemoteDocument("home-schema.json", 0);
  rapidjson::IStreamWrapper wrapper(aInput);
  rapidjson::Document dom = ReadJsonDocument(wrapper);
  ValidateJsonDocument(dom, schema);

  ApiHome result;
  const rapidjson::Value& collection = dom["data"].MemberBegin()->value;
  result.Text = ReadFuzzyText(collection["text"]);

  for (const auto& container : collection["containers"].GetArray()) {
    const rapidjson::Value& set = container["set"];

    if (strcmp(set["type"].GetString(), "SetRef") == 0)
      result.Containers.emplace_back(ReadFuzzySetRef(set));
    else
      result.Containers.emplace_back(ReadFuzzySet(set));
  }

  return result;
}
