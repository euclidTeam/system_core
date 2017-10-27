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

#include <gtest/gtest.h>

namespace android {
namespace properties {

TEST(propertycontextserializer, TrieNodeCheck) {
  auto prefixes_with_context = std::vector<std::pair<std::string, std::string>>{
      {"test.", "1st"},
      {"test.test", "2nd"},
  };
  auto exact_matches_with_context = std::vector<std::pair<std::string, std::string>>{
      {"test.test1", "3rd"},
      {"test.test2", "3rd"},
      {"test.test3", "3rd"},
      {"this.is.a.long.string", "4th"},
  };

  auto serialized_trie = std::string();
  auto build_trie_error = std::string();
  ASSERT_TRUE(BuildTrie(prefixes_with_context, exact_matches_with_context, "default",
                        &serialized_trie, &build_trie_error)) << build_trie_error;

  auto property_context_area = reinterpret_cast<const PropertyContextArea*>(serialized_trie.data());

  // Initial checks for property area.
  EXPECT_EQ(1U, property_context_area->version());

  // Check the root node
  auto root_node = property_context_area->root_node();
  EXPECT_STREQ("root", root_node.name());
  EXPECT_STREQ("default", property_context_area->context(root_node.context_index()));

  EXPECT_EQ(0U, root_node.num_prefixes());
  EXPECT_EQ(0U, root_node.num_exact_matches());

  ASSERT_EQ(2U, root_node.num_child_nodes());

  // Check the 'test'. node
  TrieNode test_node;
  ASSERT_TRUE(root_node.FindChildForString("test", 4, &test_node));

  EXPECT_STREQ("test", test_node.name());
  EXPECT_STREQ("1st", property_context_area->context(test_node.context_index()));

  EXPECT_EQ(0U, test_node.num_child_nodes());

  EXPECT_EQ(1U, test_node.num_prefixes());
  EXPECT_STREQ("test", test_node.prefix(0));
  EXPECT_STREQ("2nd", property_context_area->context(test_node.prefix_context_index(0)));

  EXPECT_EQ(3U, test_node.num_exact_matches());
  EXPECT_STREQ("test1", test_node.exact_match(0));
  EXPECT_STREQ("test2", test_node.exact_match(1));
  EXPECT_STREQ("test3", test_node.exact_match(2));

  EXPECT_STREQ("3rd", property_context_area->context(test_node.exact_match_context_index(0)));
  EXPECT_STREQ("3rd", property_context_area->context(test_node.exact_match_context_index(1)));
  EXPECT_STREQ("3rd", property_context_area->context(test_node.exact_match_context_index(2)));

  // Check the long string node
  auto expect_empty_one_child = [](auto& node) {
    EXPECT_EQ(-1U, node.context_index());
    EXPECT_EQ(0U, node.num_prefixes());
    EXPECT_EQ(0U, node.num_exact_matches());
    EXPECT_EQ(1U, node.num_child_nodes());
  };

  // Start with 'this'
  TrieNode long_string_node;
  ASSERT_TRUE(root_node.FindChildForString("this", 4, &long_string_node));
  expect_empty_one_child(long_string_node);

  // Move to 'is'
  ASSERT_TRUE(long_string_node.FindChildForString("is", 2, &long_string_node));
  expect_empty_one_child(long_string_node);

  // Move to 'a'
  ASSERT_TRUE(long_string_node.FindChildForString("a", 1, &long_string_node));
  expect_empty_one_child(long_string_node);

  // Move to 'long'
  ASSERT_TRUE(long_string_node.FindChildForString("long", 4, &long_string_node));
  EXPECT_EQ(0U, long_string_node.num_prefixes());
  EXPECT_EQ(1U, long_string_node.num_exact_matches());
  EXPECT_EQ(0U, long_string_node.num_child_nodes());

  EXPECT_STREQ("string", long_string_node.exact_match(0));
  EXPECT_STREQ("4th", property_context_area->context(long_string_node.exact_match_context_index(0)));
}

TEST(propertycontextserializer, FindContextForProperty) {
  auto prefixes_with_context = std::vector<std::pair<std::string, std::string>>{
      {"test.", "1st"},
      {"test.test", "2nd"},
      {"test.test2.", "6th"},
  };
  auto exact_matches_with_context = std::vector<std::pair<std::string, std::string>>{
      {"test.test", "5th"},
      {"test.test1", "3rd"},
      {"test.test2", "7th"},
      {"test.test3", "3rd"},
      {"this.is.a.long.string", "4th"},
  };

  auto serialized_trie = std::string();
  auto build_trie_error = std::string();
  ASSERT_TRUE(BuildTrie(prefixes_with_context, exact_matches_with_context, "default",
                        &serialized_trie, &build_trie_error)) << build_trie_error;

  auto property_context_area = reinterpret_cast<const PropertyContextArea*>(serialized_trie.data());

  // Sanity check
  auto root_node = property_context_area->root_node();
  ASSERT_STREQ("root", root_node.name());
  ASSERT_STREQ("default", property_context_area->context(root_node.context_index()));

  EXPECT_STREQ("default", property_context_area->FindContextForProperty("abc"));
  EXPECT_STREQ("default", property_context_area->FindContextForProperty("abc.abc"));
  EXPECT_STREQ("default", property_context_area->FindContextForProperty("123.abc"));

  EXPECT_STREQ("1st", property_context_area->FindContextForProperty("test.a"));
  EXPECT_STREQ("1st", property_context_area->FindContextForProperty("test.b"));
  EXPECT_STREQ("1st", property_context_area->FindContextForProperty("test.c"));

  EXPECT_STREQ("5th", property_context_area->FindContextForProperty("test.test"));
  EXPECT_STREQ("2nd", property_context_area->FindContextForProperty("test.testa"));
  EXPECT_STREQ("2nd", property_context_area->FindContextForProperty("test.testb"));
  EXPECT_STREQ("2nd", property_context_area->FindContextForProperty("test.testc"));

  EXPECT_STREQ("2nd", property_context_area->FindContextForProperty("test.test.a"));
  EXPECT_STREQ("2nd", property_context_area->FindContextForProperty("test.test.b"));
  EXPECT_STREQ("2nd", property_context_area->FindContextForProperty("test.test.c"));

  EXPECT_STREQ("3rd", property_context_area->FindContextForProperty("test.test1"));
  EXPECT_STREQ("7th", property_context_area->FindContextForProperty("test.test2"));
  EXPECT_STREQ("3rd", property_context_area->FindContextForProperty("test.test3"));

  EXPECT_STREQ("2nd", property_context_area->FindContextForProperty("test.test11"));
  EXPECT_STREQ("2nd", property_context_area->FindContextForProperty("test.test22"));
  EXPECT_STREQ("2nd", property_context_area->FindContextForProperty("test.test33"));

  EXPECT_STREQ("4th", property_context_area->FindContextForProperty("this.is.a.long.string"));

  EXPECT_STREQ("default", property_context_area->FindContextForProperty("this.is.a.long"));
  EXPECT_STREQ("default", property_context_area->FindContextForProperty("this.is.a"));
  EXPECT_STREQ("default", property_context_area->FindContextForProperty("this.is"));
  EXPECT_STREQ("default", property_context_area->FindContextForProperty("this"));

  EXPECT_STREQ("6th", property_context_area->FindContextForProperty("test.test2.a"));
}

TEST(propertycontextserializer, RealProperties) {
  auto prefixes_with_context = std::vector<std::pair<std::string, std::string>>{
      // Contexts from system/sepolicy/private/property_contexts
      {"net.rmnet", "u:object_r:net_radio_prop:s0"},
      {"net.gprs", "u:object_r:net_radio_prop:s0"},
      {"net.ppp", "u:object_r:net_radio_prop:s0"},
      {"net.qmi", "u:object_r:net_radio_prop:s0"},
      {"net.lte", "u:object_r:net_radio_prop:s0"},
      {"net.cdma", "u:object_r:net_radio_prop:s0"},
      {"net.dns", "u:object_r:net_dns_prop:s0"},
      {"sys.usb.config", "u:object_r:system_radio_prop:s0"},
      {"ril.", "u:object_r:radio_prop:s0"},
      {"ro.ril.", "u:object_r:radio_prop:s0"},
      {"gsm.", "u:object_r:radio_prop:s0"},
      {"persist.radio", "u:object_r:radio_prop:s0"},

      {"net.", "u:object_r:system_prop:s0"},
      {"dev.", "u:object_r:system_prop:s0"},
      {"ro.runtime.", "u:object_r:system_prop:s0"},
      {"ro.runtime.firstboot", "u:object_r:firstboot_prop:s0"},
      {"hw.", "u:object_r:system_prop:s0"},
      {"ro.hw.", "u:object_r:system_prop:s0"},
      {"sys.", "u:object_r:system_prop:s0"},
      {"sys.cppreopt", "u:object_r:cppreopt_prop:s0"},
      {"sys.powerctl", "u:object_r:powerctl_prop:s0"},
      {"sys.usb.ffs.", "u:object_r:ffs_prop:s0"},
      {"service.", "u:object_r:system_prop:s0"},
      {"dhcp.", "u:object_r:dhcp_prop:s0"},
      {"dhcp.bt-pan.result", "u:object_r:pan_result_prop:s0"},
      {"bluetooth.", "u:object_r:bluetooth_prop:s0"},

      {"debug.", "u:object_r:debug_prop:s0"},
      {"debug.db.", "u:object_r:debuggerd_prop:s0"},
      {"dumpstate.", "u:object_r:dumpstate_prop:s0"},
      {"dumpstate.options", "u:object_r:dumpstate_options_prop:s0"},
      {"log.", "u:object_r:log_prop:s0"},
      {"log.tag", "u:object_r:log_tag_prop:s0"},
      {"log.tag.WifiHAL", "u:object_r:wifi_log_prop:s0"},
      {"security.perf_harden", "u:object_r:shell_prop:s0"},
      {"service.adb.root", "u:object_r:shell_prop:s0"},
      {"service.adb.tcp.port", "u:object_r:shell_prop:s0"},

      {"persist.audio.", "u:object_r:audio_prop:s0"},
      {"persist.bluetooth.", "u:object_r:bluetooth_prop:s0"},
      {"persist.debug.", "u:object_r:persist_debug_prop:s0"},
      {"persist.logd.", "u:object_r:logd_prop:s0"},
      {"persist.logd.security", "u:object_r:device_logging_prop:s0"},
      {"persist.logd.logpersistd", "u:object_r:logpersistd_logging_prop:s0"},
      {"logd.logpersistd", "u:object_r:logpersistd_logging_prop:s0"},
      {"persist.log.tag", "u:object_r:log_tag_prop:s0"},
      {"persist.mmc.", "u:object_r:mmc_prop:s0"},
      {"persist.netd.stable_secret", "u:object_r:netd_stable_secret_prop:s0"},
      {"persist.sys.", "u:object_r:system_prop:s0"},
      {"persist.sys.safemode", "u:object_r:safemode_prop:s0"},
      {"ro.sys.safemode", "u:object_r:safemode_prop:s0"},
      {"persist.sys.audit_safemode", "u:object_r:safemode_prop:s0"},
      {"persist.service.", "u:object_r:system_prop:s0"},
      {"persist.service.bdroid.", "u:object_r:bluetooth_prop:s0"},
      {"persist.security.", "u:object_r:system_prop:s0"},
      {"persist.vendor.overlay.", "u:object_r:overlay_prop:s0"},
      {"ro.boot.vendor.overlay.", "u:object_r:overlay_prop:s0"},
      {"ro.boottime.", "u:object_r:boottime_prop:s0"},
      {"ro.serialno", "u:object_r:serialno_prop:s0"},
      {"ro.boot.btmacaddr", "u:object_r:bluetooth_prop:s0"},
      {"ro.boot.serialno", "u:object_r:serialno_prop:s0"},
      {"ro.bt.", "u:object_r:bluetooth_prop:s0"},
      {"ro.boot.bootreason", "u:object_r:bootloader_boot_reason_prop:s0"},
      {"persist.sys.boot.reason", "u:object_r:last_boot_reason_prop:s0"},
      {"sys.boot.reason", "u:object_r:system_boot_reason_prop:s0"},
      {"ro.device_owner", "u:object_r:device_logging_prop:s0"},

      {"selinux.restorecon_recursive", "u:object_r:restorecon_prop:s0"},

      {"vold.", "u:object_r:vold_prop:s0"},
      {"ro.crypto.", "u:object_r:vold_prop:s0"},

      {"ro.build.fingerprint", "u:object_r:fingerprint_prop:s0"},

      {"ro.persistent_properties.ready", "u:object_r:persistent_properties_ready_prop:s0"},

      {"ctl.bootanim", "u:object_r:ctl_bootanim_prop:s0"},
      {"ctl.dumpstate", "u:object_r:ctl_dumpstate_prop:s0"},
      {"ctl.fuse_", "u:object_r:ctl_fuse_prop:s0"},
      {"ctl.mdnsd", "u:object_r:ctl_mdnsd_prop:s0"},
      {"ctl.ril-daemon", "u:object_r:ctl_rildaemon_prop:s0"},
      {"ctl.bugreport", "u:object_r:ctl_bugreport_prop:s0"},
      {"ctl.console", "u:object_r:ctl_console_prop:s0"},
      {"ctl.", "u:object_r:ctl_default_prop:s0"},

      {"nfc.", "u:object_r:nfc_prop:s0"},

      {"config.", "u:object_r:config_prop:s0"},
      {"ro.config.", "u:object_r:config_prop:s0"},
      {"dalvik.", "u:object_r:dalvik_prop:s0"},
      {"ro.dalvik.", "u:object_r:dalvik_prop:s0"},

      {"wlan.", "u:object_r:wifi_prop:s0"},

      {"lowpan.", "u:object_r:lowpan_prop:s0"},
      {"ro.lowpan.", "u:object_r:lowpan_prop:s0"},

      {"hwservicemanager.", "u:object_r:hwservicemanager_prop:s0"},
      // Contexts from device/lge/bullhead/sepolicy/property_contexts
      {"wc_transport.", "u:object_r:wc_transport_prop:s0"},
      {"sys.listeners.", "u:object_r:qseecomtee_prop:s0"},
      {"sys.keymaster.", "u:object_r:qseecomtee_prop:s0"},
      {"radio.atfwd.", "u:object_r:radio_atfwd_prop:s0"},
      {"sys.ims.", "u:object_r:qcom_ims_prop:s0"},
      {"sensors.contexthub.", "u:object_r:contexthub_prop:s0"},
      {"net.r_rmnet", "u:object_r:net_radio_prop:s0"},
  };

  auto exact_matches_with_context = std::vector<std::pair<std::string, std::string>>{};

  auto serialized_trie = std::string();
  auto build_trie_error = std::string();
  ASSERT_TRUE(BuildTrie(prefixes_with_context, exact_matches_with_context,
                        "u:object_r:default_prop:s0",
                        &serialized_trie, &build_trie_error)) << build_trie_error;

  auto property_context_area = reinterpret_cast<const PropertyContextArea*>(serialized_trie.data());

  auto properties_and_contexts = std::vector<std::pair<std::string, std::string>>{
      // Actual properties on bullhead via `getprop -Z`
      {"af.fast_track_multiplier", "u:object_r:default_prop:s0"},
      {"audio_hal.period_size", "u:object_r:default_prop:s0"},
      {"bluetooth.enable_timeout_ms", "u:object_r:bluetooth_prop:s0"},
      {"dalvik.vm.appimageformat", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.boot-dex2oat-threads", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.dex2oat-Xms", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.dex2oat-Xmx", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.dex2oat-threads", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.dexopt.secondary", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.heapgrowthlimit", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.heapmaxfree", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.heapminfree", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.heapsize", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.heapstartsize", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.heaptargetutilization", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.image-dex2oat-Xms", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.image-dex2oat-Xmx", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.image-dex2oat-threads", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.isa.arm.features", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.isa.arm.variant", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.isa.arm64.features", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.isa.arm64.variant", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.lockprof.threshold", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.stack-trace-file", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.usejit", "u:object_r:dalvik_prop:s0"},
      {"dalvik.vm.usejitprofiles", "u:object_r:dalvik_prop:s0"},
      {"debug.atrace.tags.enableflags", "u:object_r:debug_prop:s0"},
      {"debug.force_rtl", "u:object_r:debug_prop:s0"},
      {"dev.bootcomplete", "u:object_r:system_prop:s0"},
      {"drm.service.enabled", "u:object_r:default_prop:s0"},
      {"gsm.current.phone-type", "u:object_r:radio_prop:s0"},
      {"gsm.network.type", "u:object_r:radio_prop:s0"},
      {"gsm.operator.alpha", "u:object_r:radio_prop:s0"},
      {"gsm.operator.iso-country", "u:object_r:radio_prop:s0"},
      {"gsm.operator.isroaming", "u:object_r:radio_prop:s0"},
      {"gsm.operator.numeric", "u:object_r:radio_prop:s0"},
      {"gsm.sim.operator.alpha", "u:object_r:radio_prop:s0"},
      {"gsm.sim.operator.iso-country", "u:object_r:radio_prop:s0"},
      {"gsm.sim.operator.numeric", "u:object_r:radio_prop:s0"},
      {"gsm.sim.state", "u:object_r:radio_prop:s0"},
      {"gsm.version.baseband", "u:object_r:radio_prop:s0"},
      {"gsm.version.ril-impl", "u:object_r:radio_prop:s0"},
      {"hwservicemanager.ready", "u:object_r:hwservicemanager_prop:s0"},
      {"init.svc.adbd", "u:object_r:default_prop:s0"},
      {"init.svc.atfwd", "u:object_r:default_prop:s0"},
      {"init.svc.audioserver", "u:object_r:default_prop:s0"},
      {"init.svc.bootanim", "u:object_r:default_prop:s0"},
      {"init.svc.bullhead-sh", "u:object_r:default_prop:s0"},
      {"init.svc.cameraserver", "u:object_r:default_prop:s0"},
      {"init.svc.cnd", "u:object_r:default_prop:s0"},
      {"init.svc.cnss-daemon", "u:object_r:default_prop:s0"},
      {"init.svc.cnss_diag", "u:object_r:default_prop:s0"},
      {"init.svc.configstore-hal-1-0", "u:object_r:default_prop:s0"},
      {"init.svc.console", "u:object_r:default_prop:s0"},
      {"init.svc.devstart_sh", "u:object_r:default_prop:s0"},
      {"init.svc.drm", "u:object_r:default_prop:s0"},
      {"init.svc.dumpstate-1-0", "u:object_r:default_prop:s0"},
      {"init.svc.flash-nanohub-fw", "u:object_r:default_prop:s0"},
      {"init.svc.fps_hal", "u:object_r:default_prop:s0"},
      {"init.svc.gatekeeperd", "u:object_r:default_prop:s0"},
      {"init.svc.gralloc-2-0", "u:object_r:default_prop:s0"},
      {"init.svc.healthd", "u:object_r:default_prop:s0"},
      {"init.svc.hidl_memory", "u:object_r:default_prop:s0"},
      {"init.svc.hostapd", "u:object_r:default_prop:s0"},
      {"init.svc.hwservicemanager", "u:object_r:default_prop:s0"},
      {"init.svc.imsdatadaemon", "u:object_r:default_prop:s0"},
      {"init.svc.imsqmidaemon", "u:object_r:default_prop:s0"},
      {"init.svc.installd", "u:object_r:default_prop:s0"},
      {"init.svc.irsc_util", "u:object_r:default_prop:s0"},
      {"init.svc.keystore", "u:object_r:default_prop:s0"},
      {"init.svc.lmkd", "u:object_r:default_prop:s0"},
      {"init.svc.loc_launcher", "u:object_r:default_prop:s0"},
      {"init.svc.logd", "u:object_r:default_prop:s0"},
      {"init.svc.logd-reinit", "u:object_r:default_prop:s0"},
      {"init.svc.media", "u:object_r:default_prop:s0"},
      {"init.svc.mediadrm", "u:object_r:default_prop:s0"},
      {"init.svc.mediaextractor", "u:object_r:default_prop:s0"},
      {"init.svc.mediametrics", "u:object_r:default_prop:s0"},
      {"init.svc.msm_irqbalance", "u:object_r:default_prop:s0"},
      {"init.svc.netd", "u:object_r:default_prop:s0"},
      {"init.svc.netmgrd", "u:object_r:default_prop:s0"},
      {"init.svc.per_mgr", "u:object_r:default_prop:s0"},
      {"init.svc.per_proxy", "u:object_r:default_prop:s0"},
      {"init.svc.perfd", "u:object_r:default_prop:s0"},
      {"init.svc.qcamerasvr", "u:object_r:default_prop:s0"},
      {"init.svc.qmuxd", "u:object_r:default_prop:s0"},
      {"init.svc.qseecomd", "u:object_r:default_prop:s0"},
      {"init.svc.qti", "u:object_r:default_prop:s0"},
      {"init.svc.ril-daemon", "u:object_r:default_prop:s0"},
      {"init.svc.rmt_storage", "u:object_r:default_prop:s0"},
      {"init.svc.servicemanager", "u:object_r:default_prop:s0"},
      {"init.svc.ss_ramdump", "u:object_r:default_prop:s0"},
      {"init.svc.start_hci_filter", "u:object_r:default_prop:s0"},
      {"init.svc.storaged", "u:object_r:default_prop:s0"},
      {"init.svc.surfaceflinger", "u:object_r:default_prop:s0"},
      {"init.svc.thermal-engine", "u:object_r:default_prop:s0"},
      {"init.svc.time_daemon", "u:object_r:default_prop:s0"},
      {"init.svc.tombstoned", "u:object_r:default_prop:s0"},
      {"init.svc.ueventd", "u:object_r:default_prop:s0"},
      {"init.svc.update_engine", "u:object_r:default_prop:s0"},
      {"init.svc.usb-hal-1-0", "u:object_r:default_prop:s0"},
      {"init.svc.vndservicemanager", "u:object_r:default_prop:s0"},
      {"init.svc.vold", "u:object_r:default_prop:s0"},
      {"init.svc.webview_zygote32", "u:object_r:default_prop:s0"},
      {"init.svc.wifi_hal_legacy", "u:object_r:default_prop:s0"},
      {"init.svc.wificond", "u:object_r:default_prop:s0"},
      {"init.svc.wpa_supplicant", "u:object_r:default_prop:s0"},
      {"init.svc.zygote", "u:object_r:default_prop:s0"},
      {"init.svc.zygote_secondary", "u:object_r:default_prop:s0"},
      {"keyguard.no_require_sim", "u:object_r:default_prop:s0"},
      {"log.tag.WifiHAL", "u:object_r:wifi_log_prop:s0"},
      {"logd.logpersistd.enable", "u:object_r:logpersistd_logging_prop:s0"},
      {"media.aac_51_output_enabled", "u:object_r:default_prop:s0"},
      {"media.recorder.show_manufacturer_and_model", "u:object_r:default_prop:s0"},
      {"net.bt.name", "u:object_r:system_prop:s0"},
      {"net.lte.ims.data.enabled", "u:object_r:net_radio_prop:s0"},
      {"net.qtaguid_enabled", "u:object_r:system_prop:s0"},
      {"net.tcp.default_init_rwnd", "u:object_r:system_prop:s0"},
      {"nfc.initialized", "u:object_r:nfc_prop:s0"},
      {"persist.audio.fluence.speaker", "u:object_r:audio_prop:s0"},
      {"persist.audio.fluence.voicecall", "u:object_r:audio_prop:s0"},
      {"persist.audio.fluence.voicecomm", "u:object_r:audio_prop:s0"},
      {"persist.audio.fluence.voicerec", "u:object_r:audio_prop:s0"},
      {"persist.camera.tnr.preview", "u:object_r:default_prop:s0"},
      {"persist.camera.tnr.video", "u:object_r:default_prop:s0"},
      {"persist.data.iwlan.enable", "u:object_r:default_prop:s0"},
      {"persist.hwc.mdpcomp.enable", "u:object_r:default_prop:s0"},
      {"persist.logd.logpersistd", "u:object_r:logpersistd_logging_prop:s0"},
      {"persist.media.treble_omx", "u:object_r:default_prop:s0"},
      {"persist.qcril.disable_retry", "u:object_r:default_prop:s0"},
      {"persist.radio.adb_log_on", "u:object_r:radio_prop:s0"},
      {"persist.radio.always_send_plmn", "u:object_r:radio_prop:s0"},
      {"persist.radio.apm_sim_not_pwdn", "u:object_r:radio_prop:s0"},
      {"persist.radio.custom_ecc", "u:object_r:radio_prop:s0"},
      {"persist.radio.data_con_rprt", "u:object_r:radio_prop:s0"},
      {"persist.radio.data_no_toggle", "u:object_r:radio_prop:s0"},
      {"persist.radio.eons.enabled", "u:object_r:radio_prop:s0"},
      {"persist.radio.eri64_as_home", "u:object_r:radio_prop:s0"},
      {"persist.radio.mode_pref_nv10", "u:object_r:radio_prop:s0"},
      {"persist.radio.process_sups_ind", "u:object_r:radio_prop:s0"},
      {"persist.radio.redir_party_num", "u:object_r:radio_prop:s0"},
      {"persist.radio.ril_payload_on", "u:object_r:radio_prop:s0"},
      {"persist.radio.snapshot_enabled", "u:object_r:radio_prop:s0"},
      {"persist.radio.snapshot_timer", "u:object_r:radio_prop:s0"},
      {"persist.radio.use_cc_names", "u:object_r:radio_prop:s0"},
      {"persist.speaker.prot.enable", "u:object_r:default_prop:s0"},
      {"persist.sys.boot.reason", "u:object_r:last_boot_reason_prop:s0"},
      {"persist.sys.dalvik.vm.lib.2", "u:object_r:system_prop:s0"},
      {"persist.sys.debug.color_temp", "u:object_r:system_prop:s0"},
      {"persist.sys.preloads.file_cache_expired", "u:object_r:system_prop:s0"},
      {"persist.sys.timezone", "u:object_r:system_prop:s0"},
      {"persist.sys.usb.config", "u:object_r:system_prop:s0"},
      {"persist.sys.webview.vmsize", "u:object_r:system_prop:s0"},
      {"persist.tom", "u:object_r:default_prop:s0"},
      {"persist.tom2", "u:object_r:default_prop:s0"},
      {"pm.dexopt.ab-ota", "u:object_r:default_prop:s0"},
      {"pm.dexopt.bg-dexopt", "u:object_r:default_prop:s0"},
      {"pm.dexopt.boot", "u:object_r:default_prop:s0"},
      {"pm.dexopt.first-boot", "u:object_r:default_prop:s0"},
      {"pm.dexopt.install", "u:object_r:default_prop:s0"},
      {"qcom.bluetooth.soc", "u:object_r:default_prop:s0"},
      {"radio.atfwd.start", "u:object_r:radio_atfwd_prop:s0"},
      {"ril.ecclist", "u:object_r:radio_prop:s0"},
      {"ril.nosim.ecc_list_1", "u:object_r:radio_prop:s0"},
      {"ril.nosim.ecc_list_count", "u:object_r:radio_prop:s0"},
      {"ril.qcril_pre_init_lock_held", "u:object_r:radio_prop:s0"},
      {"rild.libpath", "u:object_r:default_prop:s0"},
      {"ro.allow.mock.location", "u:object_r:default_prop:s0"},
      {"ro.audio.flinger_standbytime_ms", "u:object_r:default_prop:s0"},
      {"ro.baseband", "u:object_r:default_prop:s0"},
      {"ro.bionic.ld.warning", "u:object_r:default_prop:s0"},
      {"ro.board.platform", "u:object_r:default_prop:s0"},
      {"ro.boot.baseband", "u:object_r:default_prop:s0"},
      {"ro.boot.bootloader", "u:object_r:default_prop:s0"},
      {"ro.boot.bootreason", "u:object_r:bootloader_boot_reason_prop:s0"},
      {"ro.boot.dlcomplete", "u:object_r:default_prop:s0"},
      {"ro.boot.emmc", "u:object_r:default_prop:s0"},
      {"ro.boot.flash.locked", "u:object_r:default_prop:s0"},
      {"ro.boot.hardware", "u:object_r:default_prop:s0"},
      {"ro.boot.hardware.sku", "u:object_r:default_prop:s0"},
      {"ro.boot.revision", "u:object_r:default_prop:s0"},
      {"ro.boot.serialno", "u:object_r:serialno_prop:s0"},
      {"ro.boot.verifiedbootstate", "u:object_r:default_prop:s0"},
      {"ro.boot.veritymode", "u:object_r:default_prop:s0"},
      {"ro.boot.wificountrycode", "u:object_r:default_prop:s0"},
      {"ro.bootimage.build.date", "u:object_r:default_prop:s0"},
      {"ro.bootimage.build.date.utc", "u:object_r:default_prop:s0"},
      {"ro.bootimage.build.fingerprint", "u:object_r:default_prop:s0"},
      {"ro.bootloader", "u:object_r:default_prop:s0"},
      {"ro.bootmode", "u:object_r:default_prop:s0"},
      {"ro.boottime.adbd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.atfwd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.audioserver", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.bootanim", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.bullhead-sh", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.cameraserver", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.cnd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.cnss-daemon", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.cnss_diag", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.configstore-hal-1-0", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.console", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.devstart_sh", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.drm", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.dumpstate-1-0", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.flash-nanohub-fw", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.fps_hal", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.gatekeeperd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.gralloc-2-0", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.healthd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.hidl_memory", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.hwservicemanager", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.imsdatadaemon", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.imsqmidaemon", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.init", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.init.cold_boot_wait", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.init.mount_all.default", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.init.selinux", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.installd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.irsc_util", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.keystore", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.lmkd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.loc_launcher", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.logd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.logd-reinit", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.media", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.mediadrm", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.mediaextractor", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.mediametrics", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.msm_irqbalance", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.netd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.netmgrd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.per_mgr", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.per_proxy", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.perfd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.qcamerasvr", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.qmuxd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.qseecomd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.qti", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.ril-daemon", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.rmt_storage", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.servicemanager", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.ss_ramdump", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.start_hci_filter", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.storaged", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.surfaceflinger", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.thermal-engine", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.time_daemon", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.tombstoned", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.ueventd", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.update_engine", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.usb-hal-1-0", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.vndservicemanager", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.vold", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.webview_zygote32", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.wifi_hal_legacy", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.wificond", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.zygote", "u:object_r:boottime_prop:s0"},
      {"ro.boottime.zygote_secondary", "u:object_r:boottime_prop:s0"},
      {"ro.bt.bdaddr_path", "u:object_r:bluetooth_prop:s0"},
      {"ro.build.characteristics", "u:object_r:default_prop:s0"},
      {"ro.build.date", "u:object_r:default_prop:s0"},
      {"ro.build.date.utc", "u:object_r:default_prop:s0"},
      {"ro.build.description", "u:object_r:default_prop:s0"},
      {"ro.build.display.id", "u:object_r:default_prop:s0"},
      {"ro.build.expect.baseband", "u:object_r:default_prop:s0"},
      {"ro.build.expect.bootloader", "u:object_r:default_prop:s0"},
      {"ro.build.fingerprint", "u:object_r:fingerprint_prop:s0"},
      {"ro.build.flavor", "u:object_r:default_prop:s0"},
      {"ro.build.host", "u:object_r:default_prop:s0"},
      {"ro.build.id", "u:object_r:default_prop:s0"},
      {"ro.build.product", "u:object_r:default_prop:s0"},
      {"ro.build.tags", "u:object_r:default_prop:s0"},
      {"ro.build.type", "u:object_r:default_prop:s0"},
      {"ro.build.user", "u:object_r:default_prop:s0"},
      {"ro.build.version.all_codenames", "u:object_r:default_prop:s0"},
      {"ro.build.version.base_os", "u:object_r:default_prop:s0"},
      {"ro.build.version.codename", "u:object_r:default_prop:s0"},
      {"ro.build.version.incremental", "u:object_r:default_prop:s0"},
      {"ro.build.version.preview_sdk", "u:object_r:default_prop:s0"},
      {"ro.build.version.release", "u:object_r:default_prop:s0"},
      {"ro.build.version.sdk", "u:object_r:default_prop:s0"},
      {"ro.build.version.security_patch", "u:object_r:default_prop:s0"},
      {"ro.camera.notify_nfc", "u:object_r:default_prop:s0"},
      {"ro.carrier", "u:object_r:default_prop:s0"},
      {"ro.com.android.dataroaming", "u:object_r:default_prop:s0"},
      {"ro.config.alarm_alert", "u:object_r:config_prop:s0"},
      {"ro.config.notification_sound", "u:object_r:config_prop:s0"},
      {"ro.config.ringtone", "u:object_r:config_prop:s0"},
      {"ro.config.vc_call_vol_steps", "u:object_r:config_prop:s0"},
      {"ro.crypto.fs_crypto_blkdev", "u:object_r:vold_prop:s0"},
      {"ro.crypto.state", "u:object_r:vold_prop:s0"},
      {"ro.crypto.type", "u:object_r:vold_prop:s0"},
      {"ro.dalvik.vm.native.bridge", "u:object_r:dalvik_prop:s0"},
      {"ro.debuggable", "u:object_r:default_prop:s0"},
      {"ro.device_owner", "u:object_r:device_logging_prop:s0"},
      {"ro.expect.recovery_id", "u:object_r:default_prop:s0"},
      {"ro.frp.pst", "u:object_r:default_prop:s0"},
      {"ro.hardware", "u:object_r:default_prop:s0"},
      {"ro.hwui.drop_shadow_cache_size", "u:object_r:default_prop:s0"},
      {"ro.hwui.gradient_cache_size", "u:object_r:default_prop:s0"},
      {"ro.hwui.layer_cache_size", "u:object_r:default_prop:s0"},
      {"ro.hwui.path_cache_size", "u:object_r:default_prop:s0"},
      {"ro.hwui.r_buffer_cache_size", "u:object_r:default_prop:s0"},
      {"ro.hwui.text_large_cache_height", "u:object_r:default_prop:s0"},
      {"ro.hwui.text_large_cache_width", "u:object_r:default_prop:s0"},
      {"ro.hwui.text_small_cache_height", "u:object_r:default_prop:s0"},
      {"ro.hwui.text_small_cache_width", "u:object_r:default_prop:s0"},
      {"ro.hwui.texture_cache_flushrate", "u:object_r:default_prop:s0"},
      {"ro.hwui.texture_cache_size", "u:object_r:default_prop:s0"},
      {"ro.min_freq_0", "u:object_r:default_prop:s0"},
      {"ro.min_freq_4", "u:object_r:default_prop:s0"},
      {"ro.oem_unlock_supported", "u:object_r:default_prop:s0"},
      {"ro.opengles.version", "u:object_r:default_prop:s0"},
      {"ro.persistent_properties.ready", "u:object_r:persistent_properties_ready_prop:s0"},
      {"ro.product.board", "u:object_r:default_prop:s0"},
      {"ro.product.brand", "u:object_r:default_prop:s0"},
      {"ro.product.cpu.abi", "u:object_r:default_prop:s0"},
      {"ro.product.cpu.abilist", "u:object_r:default_prop:s0"},
      {"ro.product.cpu.abilist32", "u:object_r:default_prop:s0"},
      {"ro.product.cpu.abilist64", "u:object_r:default_prop:s0"},
      {"ro.product.device", "u:object_r:default_prop:s0"},
      {"ro.product.first_api_level", "u:object_r:default_prop:s0"},
      {"ro.product.locale", "u:object_r:default_prop:s0"},
      {"ro.product.manufacturer", "u:object_r:default_prop:s0"},
      {"ro.product.model", "u:object_r:default_prop:s0"},
      {"ro.product.name", "u:object_r:default_prop:s0"},
      {"ro.property_service.version", "u:object_r:default_prop:s0"},
      {"ro.qc.sdk.audio.fluencetype", "u:object_r:default_prop:s0"},
      {"ro.recovery_id", "u:object_r:default_prop:s0"},
      {"ro.revision", "u:object_r:default_prop:s0"},
      {"ro.ril.svdo", "u:object_r:radio_prop:s0"},
      {"ro.ril.svlte1x", "u:object_r:radio_prop:s0"},
      {"ro.runtime.firstboot", "u:object_r:firstboot_prop:s0"},
      {"ro.secure", "u:object_r:default_prop:s0"},
      {"ro.serialno", "u:object_r:serialno_prop:s0"},
      {"ro.sf.lcd_density", "u:object_r:default_prop:s0"},
      {"ro.telephony.call_ring.multiple", "u:object_r:default_prop:s0"},
      {"ro.telephony.default_cdma_sub", "u:object_r:default_prop:s0"},
      {"ro.telephony.default_network", "u:object_r:default_prop:s0"},
      {"ro.treble.enabled", "u:object_r:default_prop:s0"},
      {"ro.vendor.build.date", "u:object_r:default_prop:s0"},
      {"ro.vendor.build.date.utc", "u:object_r:default_prop:s0"},
      {"ro.vendor.build.fingerprint", "u:object_r:default_prop:s0"},
      {"ro.vendor.extension_library", "u:object_r:default_prop:s0"},
      {"ro.wifi.channels", "u:object_r:default_prop:s0"},
      {"ro.zygote", "u:object_r:default_prop:s0"},
      {"security.perf_harden", "u:object_r:shell_prop:s0"},
      {"sensors.contexthub.lid_state", "u:object_r:contexthub_prop:s0"},
      {"service.adb.root", "u:object_r:shell_prop:s0"},
      {"service.bootanim.exit", "u:object_r:system_prop:s0"},
      {"service.sf.present_timestamp", "u:object_r:system_prop:s0"},
      {"sys.boot.reason", "u:object_r:system_boot_reason_prop:s0"},
      {"sys.boot_completed", "u:object_r:system_prop:s0"},
      {"sys.ims.QMI_DAEMON_STATUS", "u:object_r:qcom_ims_prop:s0"},
      {"sys.listeners.registered", "u:object_r:qseecomtee_prop:s0"},
      {"sys.logbootcomplete", "u:object_r:system_prop:s0"},
      {"sys.oem_unlock_allowed", "u:object_r:system_prop:s0"},
      {"sys.qcom.devup", "u:object_r:system_prop:s0"},
      {"sys.sysctl.extra_free_kbytes", "u:object_r:system_prop:s0"},
      {"sys.usb.config", "u:object_r:system_radio_prop:s0"},
      {"sys.usb.configfs", "u:object_r:system_radio_prop:s0"},
      {"sys.usb.controller", "u:object_r:system_prop:s0"},
      {"sys.usb.ffs.aio_compat", "u:object_r:ffs_prop:s0"},
      {"sys.usb.ffs.max_read", "u:object_r:ffs_prop:s0"},
      {"sys.usb.ffs.max_write", "u:object_r:ffs_prop:s0"},
      {"sys.usb.ffs.ready", "u:object_r:ffs_prop:s0"},
      {"sys.usb.mtp.device_type", "u:object_r:system_prop:s0"},
      {"sys.usb.state", "u:object_r:system_prop:s0"},
      {"telephony.lteOnCdmaDevice", "u:object_r:default_prop:s0"},
      {"tombstoned.max_tombstone_count", "u:object_r:default_prop:s0"},
      {"vidc.debug.perf.mode", "u:object_r:default_prop:s0"},
      {"vidc.enc.dcvs.extra-buff-count", "u:object_r:default_prop:s0"},
      {"vold.decrypt", "u:object_r:vold_prop:s0"},
      {"vold.has_adoptable", "u:object_r:vold_prop:s0"},
      {"vold.post_fs_data_done", "u:object_r:vold_prop:s0"},
      {"wc_transport.clean_up", "u:object_r:wc_transport_prop:s0"},
      {"wc_transport.hci_filter_status", "u:object_r:wc_transport_prop:s0"},
      {"wc_transport.ref_count", "u:object_r:wc_transport_prop:s0"},
      {"wc_transport.soc_initialized", "u:object_r:wc_transport_prop:s0"},
      {"wc_transport.start_hci", "u:object_r:wc_transport_prop:s0"},
      {"wc_transport.vnd_power", "u:object_r:wc_transport_prop:s0"},
      {"wifi.interface", "u:object_r:default_prop:s0"},
      {"wifi.supplicant_scan_interval", "u:object_r:default_prop:s0"},
  };

  for (const auto& [property, context] : properties_and_contexts) {
    EXPECT_EQ(context, property_context_area->FindContextForProperty(property.c_str())) << property;
  }
}

}  // namespace properties
}  // namespace android
