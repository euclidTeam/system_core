
#pragma once

#include <memory>
#include <string>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <gtest/gtest.h>
#include <json/reader.h>
#include <json/writer.h>
#include <jsonpb/jsonpb.h>
#include <jsonpb/verify.h>

// JsonSchemaTest test that a given JSON file conforms to a given schema.
// This includes:
// - libprotobuf can parse the given JSON file using the given Prototype class
// - Additional checks on field names of the JSON file, and types of values.

namespace android {
namespace jsonpb {

class JsonSchemaTestConfig {
  public:
    virtual ~JsonSchemaTestConfig() = default;
    virtual std::unique_ptr<google::protobuf::Message> CreateMessage() const = 0;
    virtual std::string file_path() const = 0;
    virtual std::string GetFileContent() const {
        std::string content;
        if (!android::base::ReadFileToString(file_path(), &content)) {
            return "";
        }
        return content;
    }
    virtual bool verbose() const { return false; };
};
using JsonSchemaTestConfigFactory = std::function<std::unique_ptr<JsonSchemaTestConfig>()>;

template <typename T>
class AbstractJsonSchemaTestConfig : public JsonSchemaTestConfig {
  public:
    AbstractJsonSchemaTestConfig(const std::string& path, bool verbose = false)
        : file_path_(path), verbose_(verbose){};
    std::unique_ptr<google::protobuf::Message> CreateMessage() const override {
        return std::make_unique<T>();
    }
    std::string file_path() const override { return file_path_; }
    bool verbose() const override { return verbose_; }

  private:
    std::string file_path_;
    bool verbose_;
};

class JsonSchemaTest : public ::testing::TestWithParam<JsonSchemaTestConfigFactory> {
  public:
    void SetUp() override {
        auto&& config = ::testing::TestWithParam<JsonSchemaTestConfigFactory>::GetParam()();
        file_path_ = config->file_path();
        json_ = config->GetFileContent();
        ASSERT_FALSE(json_.empty()) << "Cannot read " << config->file_path();
        object_ = config->CreateMessage();
        auto res = internal::JsonStringToMessage(json_, object_.get());
        ASSERT_TRUE(res.ok()) << "Invalid format of file " << config->file_path() << ": "
                              << res.error();
        if (config->verbose()) {
            std::cout << object_->DebugString() << std::endl;
        }
    }
    std::string file_path_;
    std::string json_;
    std::unique_ptr<google::protobuf::Message> object_;
};

// Test that the JSON file has no fields unknown by the schema. See AllFieldsAreKnown() for
// more details.
TEST_P(JsonSchemaTest, NoUnknownFields) {
    std::string error;
    EXPECT_TRUE(AllFieldsAreKnown(*object_, json_, &error))
            << "File: " << file_path_ << ": " << error;
}

TEST_P(JsonSchemaTest, EqReformattedJson) {
    std::string error;
    EXPECT_TRUE(EqReformattedJson(json_, object_.get(), &error))
            << "File: " << file_path_ << ": " << error;
}

}  // namespace jsonpb
}  // namespace android
