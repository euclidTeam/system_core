/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <libgen.h>
#include <algorithm>
#include <array>

#include "android-base/file.h"
#include "client/file_sync_client.h"
#include "commandline.h"
#include "fastdeploy.h"
#include "fastdeploycallbacks.h"
#include "utils/String16.h"

const long kRequiredAgentVersion = 0x00000001;
const char* kDeviceAgentPath = "/data/local/tmp/";

struct TFastDeployConfig {
    bool use_localagent;
    std::string adb_path;
};

static TFastDeployConfig* fastdeploy_config = nullptr;

long get_agent_version() {
    std::vector<char> versionOutputBuffer;
    std::vector<char> versionErrorBuffer;

    int statusCode = capture_shell_command("/data/local/tmp/deployagent.sh version",
                                           &versionOutputBuffer, &versionErrorBuffer);
    long version = -1;

    if (statusCode == 0 && versionOutputBuffer.size() > 0) {
        version = strtol((char*)versionOutputBuffer.data(), NULL, 16);
    }

    return version;
}

int get_device_api_level() {
    std::vector<char> sdkVersionOutputBuffer;
    std::vector<char> sdkVersionErrorBuffer;
    int api_level = -1;

    int statusCode = capture_shell_command("getprop ro.build.version.sdk", &sdkVersionOutputBuffer,
                                           &sdkVersionErrorBuffer);
    if (statusCode == 0 && sdkVersionOutputBuffer.size() > 0) {
        api_level = strtol((char*)sdkVersionOutputBuffer.data(), NULL, 10);
    }

    return api_level;
}

void fastdeploy_init(bool use_localagent, const std::string& adb_path) {
    if (fastdeploy_config != nullptr) {
        printf("Warning: fastdeploy has been double initialized\n");
        delete fastdeploy_config;
    }
    fastdeploy_config = new TFastDeployConfig();
    fastdeploy_config->use_localagent = use_localagent;
    fastdeploy_config->adb_path = adb_path;
}

void fastdeploy_deinit() {
    delete fastdeploy_config;
}

// local_path - must start with a '/' and be relative to $ANDROID_PRODUCT_OUT
static bool get_agent_component_host_path(const char* local_path, const char* sdk_path,
                                          std::string* output_path) {
    if (fastdeploy_config == nullptr) {
        printf("FastDeploy being called without being Initialized\n");
        return false;
    }

    std::string mutable_adb_path = fastdeploy_config->adb_path;
    const char* adb_dir = dirname(&mutable_adb_path[0]);
    if (adb_dir == nullptr) {
        return false;
    }

    if (fastdeploy_config->use_localagent) {
        const char* product_out = getenv("ANDROID_PRODUCT_OUT");
        if (product_out == nullptr) {
            return false;
        }
        *output_path = android::base::StringPrintf("%s%s", product_out, local_path);
        return true;
    } else {
        *output_path = android::base::StringPrintf("%s%s", adb_dir, sdk_path);
        return true;
    }
    return false;
}

static bool deploy_agent(bool checkTimeStamps) {
    std::vector<const char*> srcs;
    std::string agent_jar_path;
    if (get_agent_component_host_path("/system/framework/deployagent.jar", "/deployagent.jar",
                                      &agent_jar_path)) {
        srcs.push_back(agent_jar_path.c_str());
    } else {
        return false;
    }

    std::string agent_sh_path;
    if (get_agent_component_host_path("/system/bin/deployagent.sh", "/deployagent.sh",
                                      &agent_sh_path)) {
        srcs.push_back(agent_sh_path.c_str());
    } else {
        return false;
    }

    if (do_sync_push(srcs, kDeviceAgentPath, checkTimeStamps)) {
        // on windows the shell script might have lost execute permission
        // so need to set this explicitly
        const char* kChmodCommandPattern = "chmod 777 %sdeployagent.sh";
        std::string chmodCommand =
                android::base::StringPrintf(kChmodCommandPattern, kDeviceAgentPath);
        int ret = send_shell_command(chmodCommand);
        return (ret == 0);
    } else {
        return false;
    }
}

bool update_agent(FastDeploy_AgentUpdateStrategy agentUpdateStrategy) {
    long agent_version = get_agent_version();
    switch (agentUpdateStrategy) {
        case FastDeploy_AgentUpdateAlways:
            if (deploy_agent(false) == false) {
                return false;
            }
            break;
        case FastDeploy_AgentUpdateNewerTimeStamp:
            if (deploy_agent(true) == false) {
                return false;
            }
            break;
        case FastDeploy_AgentUpdateDifferentVersion:
            if (agent_version != kRequiredAgentVersion) {
                if (agent_version < 0) {
                    printf("Could not detect agent on device, deploying\n");
                } else {
                    printf("Device agent version is (%ld), (%ld) is required, re-deploying\n",
                           agent_version, kRequiredAgentVersion);
                }
                if (deploy_agent(false) == false) {
                    return false;
                }
            }
            break;
    }

    agent_version = get_agent_version();
    return (agent_version == kRequiredAgentVersion);
}

static bool get_aapt2_path(std::string* output) {
    if (fastdeploy_config == nullptr) {
        printf("FastDeploy being called without being Initialized\n");
        return false;
    }

    if (fastdeploy_config->use_localagent) {
        // This should never happen on a Windows machine
        const char* kAapt2Pattern = "%s/bin/aapt2";
        const char* host_out = getenv("ANDROID_HOST_OUT");
        if (host_out == nullptr) {
            printf("Could not locate aapt2 because $ANDROID_HOST_OUT is not defined\n");
            return false;
        }
        *output = android::base::StringPrintf(kAapt2Pattern, host_out);
        return true;
    } else {
        const char* kAapt2Pattern = "%s/aapt2";
        std::string mutable_adb_path = fastdeploy_config->adb_path;
        const char* adb_dir = dirname(&mutable_adb_path[0]);
        if (adb_dir == nullptr) {
            return false;
        }
        *output = android::base::StringPrintf(kAapt2Pattern, adb_dir);
        return true;
    }
    return false;
}

static int system_capture(const char* cmd, std::string& output) {
    FILE* pipe = popen(cmd, "re");
    int fd = -1;

    if (pipe != nullptr) {
        fd = fileno(pipe);
    }

    if (fd == -1) {
        printf("Could not create pipe for process '%s'\n", cmd);
        return -1;
    }

    if (!android::base::ReadFdToString(fd, &output)) {
        printf("Error reading from process '%s'\n", cmd);
        return -1;
    }

    return pclose(pipe);
}

// output is required to point to a valid output string (non-null)
static bool get_packagename_from_apk(const char* apkPath, std::string* output) {
    const char* kAapt2DumpNameCommandPattern = R"(%s dump packagename "%s")";
    std::string aapt2_path_string;

    if (get_aapt2_path(&aapt2_path_string) == false) {
        return false;
    }
    std::string getPackagenameCommand = android::base::StringPrintf(
            kAapt2DumpNameCommandPattern, aapt2_path_string.c_str(), apkPath);

    if (system_capture(getPackagenameCommand.c_str(), *output) == 0) {
        // strip any line end characters from the output
        output->erase(
                std::remove_if(output->begin(), output->end(),
                               [](auto const& c) -> bool { return (c) == '\n' || (c) == '\r'; }),
                output->end());
        return true;
    }
    return false;
}

int extract_metadata(const char* apkPath, FILE* outputFp) {
    std::string packageName;
    if (get_packagename_from_apk(apkPath, &packageName) == false) {
        return -1;
    }

    const char* kAgentExtractCommandPattern = "/data/local/tmp/deployagent.sh extract %s";
    std::string extractCommand =
            android::base::StringPrintf(kAgentExtractCommandPattern, packageName.c_str());

    std::vector<char> extractErrorBuffer;
    int statusCode;
    DeployAgentFileCallback cb(outputFp, &extractErrorBuffer, &statusCode);
    int ret = send_shell_command(extractCommand, false, &cb);

    if (ret == 0) {
        return cb.getBytesWritten();
    }

    return ret;
}

// output is required to point to a valid output string (non-null)
static bool patch_generator_command(std::string* output) {
    if (fastdeploy_config == nullptr) {
        printf("FastDeploy being called without being Initialized\n");
        return false;
    }

    if (fastdeploy_config->use_localagent) {
        // This should never happen on a Windows machine
        const char* kGeneratorCommandPattern = "java -jar %s/framework/deploypatchgenerator.jar";
        const char* host_out = getenv("ANDROID_HOST_OUT");
        if (host_out == nullptr) {
            return false;
        }
        *output = android::base::StringPrintf(kGeneratorCommandPattern, host_out, host_out);
        return true;
    } else {
        const char* kGeneratorCommandPattern = R"(java -jar "%s/deploypatchgenerator.jar")";
        std::string mutable_adb_path = fastdeploy_config->adb_path;
        const char* adb_dir = dirname(&mutable_adb_path[0]);
        if (adb_dir == nullptr) {
            return false;
        }

        *output = android::base::StringPrintf(kGeneratorCommandPattern, adb_dir, adb_dir);
        return true;
    }
    return false;
}

int create_patch(const char* apkPath, const char* metadataPath, const char* patchPath) {
    const char* kGeneratePatchCommandPattern = R"(%s "%s" "%s" > "%s")";
    std::string patch_generator_command_string;
    if (patch_generator_command(&patch_generator_command_string) == false) {
        return 1;
    }
    std::string generatePatchCommand = android::base::StringPrintf(
            kGeneratePatchCommandPattern, patch_generator_command_string.c_str(), apkPath,
            metadataPath, patchPath);
    return system(generatePatchCommand.c_str());
}

std::string get_patch_path(const char* apkPath) {
    std::string packageName;
    if (get_packagename_from_apk(apkPath, &packageName) == false) {
        return "";
    }

    std::string patchDevicePath =
            android::base::StringPrintf("%s%s.patch", kDeviceAgentPath, packageName.c_str());
    return patchDevicePath;
}

int apply_patch_on_device(const char* apkPath, const char* patchPath, const char* outputPath) {
    const std::string kAgentApplyCommandPattern =
            "/data/local/tmp/deployagent.sh apply %s %s -o %s";

    std::string packageName;
    if (get_packagename_from_apk(apkPath, &packageName) == false) {
        return -1;
    }

    std::string patchDevicePath = get_patch_path(apkPath);

    std::vector<const char*> srcs = {patchPath};
    bool push_ok = do_sync_push(srcs, patchDevicePath.c_str(), false);

    if (!push_ok) {
        return -1;
    }

    std::string applyPatchCommand =
            android::base::StringPrintf(kAgentApplyCommandPattern.c_str(), packageName.c_str(),
                                        patchDevicePath.c_str(), outputPath);

    return send_shell_command(applyPatchCommand);
}

int install_patch(const char* apkPath, const char* patchPath, int argc, const char** argv) {
    const std::string kAgentApplyCommandPattern =
            "/data/local/tmp/deployagent.sh apply %s %s -pm %s";

    std::string packageName;
    if (get_packagename_from_apk(apkPath, &packageName) == false) {
        return -1;
    }

    std::vector<const char*> srcs;
    std::string patchDevicePath =
            android::base::StringPrintf("%s%s.patch", kDeviceAgentPath, packageName.c_str());
    srcs.push_back(patchPath);
    bool push_ok = do_sync_push(srcs, patchDevicePath.c_str(), false);

    if (!push_ok) {
        return -1;
    }

    std::vector<unsigned char> applyOutputBuffer;
    std::vector<unsigned char> applyErrorBuffer;
    std::string argsString;

    for (int i = 0; i < argc; i++) {
        argsString.append(argv[i]);
        argsString.append(" ");
    }

    std::string applyPatchCommand =
            android::base::StringPrintf(kAgentApplyCommandPattern.c_str(), packageName.c_str(),
                                        patchDevicePath.c_str(), argsString.c_str());
    return send_shell_command(applyPatchCommand);
}
