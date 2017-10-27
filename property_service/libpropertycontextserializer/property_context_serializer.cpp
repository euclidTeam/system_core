//
// Copyright (C) 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "property_context_serializer/property_context_serializer.h"

#include "property_context_parser/property_context_parser.h"

#include <set>

#include "trie_builder.h"
#include "trie_serializer.h"

namespace android {
namespace properties {

bool BuildTrie(const std::vector<PropertyInfoEntry>& property_contexts,
               const std::string& default_context, const std::string& default_schema,
               std::string* serialized_trie, std::string* error) {
  // Check that names are legal first
  auto trie_builder = TrieBuilder(default_context, default_schema);

  for (const auto& [name, context, schema, is_exact] : property_contexts) {
    if (!trie_builder.AddToTrie(name, context, schema, is_exact, error)) {
      return false;
    }
  }

  auto trie_serializer = TrieSerializer();
  *serialized_trie = trie_serializer.SerializeTrie(trie_builder);
  return true;
}

}  // namespace properties
}  // namespace android
