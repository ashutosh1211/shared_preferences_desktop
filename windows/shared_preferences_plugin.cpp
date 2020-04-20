// Copyright 2019 Google LLC
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

#include "shared_preferences_plugin.h"

#include <ShlObj.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <windows.h>

#include <codecvt>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "jsoncpp/json/json.h"

namespace {
using flutter::EncodableValue;

// Json file used to store key values
std::string defaultFileName = "sp_flutter.json";

// Converts an null-terminated array of wchar_t's to a std::string.
std::string StdStringFromWideChars(wchar_t *wide_chars) {
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> wide_to_utf8;
  return wide_to_utf8.to_bytes(wide_chars);
}

// Gets the path of Json file without verifying is file exist or not
// base directory will be as per the folder_id 
std::string GetFolderPath(REFKNOWNFOLDERID folder_id) {
  wchar_t *wide_path = nullptr;
  if (!SUCCEEDED(SHGetKnownFolderPath(folder_id, KF_FLAG_DONT_VERIFY, nullptr,
                                      &wide_path))) {
    return "";
  }
  std::string path = StdStringFromWideChars(wide_path) + "\\" + defaultFileName;
  CoTaskMemFree(wide_path);
  return path;
}

// Method to get path of Json file
std::string GetPrefLocation() { return GetFolderPath(FOLDERID_Documents); }

class SharedPreferencesPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrar *registrar);

  virtual ~SharedPreferencesPlugin();

 private:
  SharedPreferencesPlugin();
  Json::Value root;
  // Called when a method is called on plugin channel;
  void HandleMethodCall(
      const flutter::MethodCall<EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<EncodableValue>> result);

  EncodableValue convertToDartValue(const Json::Value &value);

  void initRoot();
  void saveRoot(Json::Value value);
  void clearFile();
  void printRoot();
  std::string getFilePath();
};

// static
void SharedPreferencesPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar *registrar) {
  auto channel = std::make_unique<flutter::MethodChannel<EncodableValue>>(
      registrar->messenger(), "plugins.flutter.io/shared_preferences",
      &flutter::StandardMethodCodec::GetInstance());

  // Uses new instead of make_unique due to private constructor.
  std::unique_ptr<SharedPreferencesPlugin> plugin(
      new SharedPreferencesPlugin());

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

SharedPreferencesPlugin::SharedPreferencesPlugin() = default;

SharedPreferencesPlugin::~SharedPreferencesPlugin() = default;

void SharedPreferencesPlugin::HandleMethodCall(
    const flutter::MethodCall<EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<EncodableValue>> result) {
  if (method_call.method_name().compare("getAll") == 0) {
    // fill root object
    initRoot();

    EncodableValue ret(EncodableValue::Type::kMap);
    flutter::EncodableMap &map = ret.MapValue();

    for (auto i = root.begin(); i != root.end(); i++) {
      Json::Value &obj = *i;
      const std::string key = i.name();
      map[EncodableValue(key)] = convertToDartValue(obj);
    }

    result->Success(&ret);
  } else if (method_call.method_name().compare("setInt") == 0 ||
             method_call.method_name().compare("setString") == 0 ||
             method_call.method_name().compare("setDouble") == 0 ||
             method_call.method_name().compare("setBool") == 0 ||
             method_call.method_name().compare("setStringList") == 0) {
    std::string key;
    std::string methodCall = method_call.method_name();

    if (method_call.arguments() && method_call.arguments()->IsMap()) {
      const flutter::EncodableMap &arguments =
          method_call.arguments()->MapValue();
      auto key_it = arguments.find(EncodableValue("key"));
      if (key_it != arguments.end()) {
        key = key_it->second.StringValue();
      }

      auto value_it = arguments.find(EncodableValue("value"));

      Json::Value valueToStore;

      // store value based on type
      if (methodCall == "setInt") {
        int value = 0;
        if (value_it != arguments.end()) {
          value = value_it->second.IntValue();
        }
        valueToStore = (new Json::Value(value))->asInt();
      } else if (methodCall == "setString") {
        std::string value = "";
        if (value_it != arguments.end()) {
          value = value_it->second.StringValue();
        }
        valueToStore = (new Json::Value(value))->asString();
      } else if (methodCall == "setDouble") {
        double value = 0.0;
        if (value_it != arguments.end()) {
          value = value_it->second.DoubleValue();
        }
        valueToStore = (new Json::Value(value))->asDouble();
      } else if (methodCall == "setBool") {
        bool value = false;
        if (value_it != arguments.end()) {
          value = value_it->second.BoolValue();
        }
        valueToStore = (new Json::Value(value))->asBool();
      } else if (methodCall == "setStringList") {
        valueToStore = new Json::Value(Json::arrayValue);
        flutter::EncodableList listValue;
        if (value_it != arguments.end()) {
          listValue = value_it->second.ListValue();
        }

        // TODO: Implment saveing of list of string
        /*for (int i = 0; i < listValue.size(); i++) {
          valueToStore.append(
              (new Json::Value(listValue[i].StringValue()))->asString());
        }*/
      }

      root[key] = valueToStore;

      flutter::EncodableValue response(true);
      result->Success();

      saveRoot(root);
    }
  } else if (method_call.method_name().compare("clear") == 0) {
    clearFile();
  } else if (method_call.method_name().compare("remove") == 0) {
    std::string key;
    if (method_call.arguments() && method_call.arguments()->IsMap()) {
      const flutter::EncodableMap &arguments =
          method_call.arguments()->MapValue();
      auto key_it = arguments.find(EncodableValue("key"));
      if (key_it != arguments.end()) {
        key = key_it->second.StringValue();
      }

      root.removeMember(key);

      flutter::EncodableValue response(true);
      result->Success();

      saveRoot(root);
    }
  } else {
    result->NotImplemented();
  }
}

EncodableValue SharedPreferencesPlugin::convertToDartValue(
    const Json::Value &value) {
  switch (value.type()) {
    case Json::nullValue: {
      return EncodableValue(EncodableValue::Type::kNull);
    }
    case Json::booleanValue: {
      bool v = value.asBool();
      return EncodableValue(v);
    }
    case Json::uintValue:
    case Json::intValue: {
      int v = value.asInt();
      return EncodableValue(v);
    }
    case Json::realValue: {
      double v = value.asDouble();
      return EncodableValue(v);
    }
    case Json::arrayValue: {
      EncodableValue ev(EncodableValue::Type::kList);
      flutter::EncodableList &v = ev.ListValue();
      Json::Value def;
      for (Json::ArrayIndex i = 0; i < value.size(); ++i) {
        v.push_back(convertToDartValue(value.get(i, def)));
      }
      return ev;
    }
    case Json::objectValue: {
      return EncodableValue();
    }
    case Json::stringValue:
    default: {
      const char *v = value.asCString();
      return EncodableValue(v);
    }
  }
}

void SharedPreferencesPlugin::initRoot() {
  std::ifstream infile(getFilePath());

  try {
    infile >> root;
  } catch (std::exception &e) {
    std::cout << e.what() << std::endl;
    root = Json::objectValue;
    saveRoot(root);
  }

  infile.close();
}

void SharedPreferencesPlugin::printRoot() {
  for (auto i = root.begin(); i != root.end(); i++) {
    Json::Value &obj = *i;
    const std::string key = i.name();
    std::cout << key << obj << std::endl;
  }
}

std::string SharedPreferencesPlugin::getFilePath() {
  std::string path = GetPrefLocation();
  std::cout << "SharedPreferencesPlugin, " << path << std::endl;
  return path;
}

void SharedPreferencesPlugin::saveRoot(Json::Value value) {
  Json::StreamWriterBuilder builder;

  builder["commentStyle"] = "None";
  builder["indentation"] = "   ";

  std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
  std::ofstream ofile(getFilePath());

  writer->write(value, &ofile);

  ofile.close();
}

void SharedPreferencesPlugin::clearFile() { saveRoot(Json::objectValue); }

}  // namespace

void SharedPreferencesPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  SharedPreferencesPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
