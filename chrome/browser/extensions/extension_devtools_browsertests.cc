// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/ref_counted.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/debugger/devtools_manager.h"
#include "chrome/browser/debugger/devtools_client_host.h"
#include "chrome/browser/extensions/extension_devtools_browsertest.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/extensions/extension_tabs_module.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/site_instance.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/views/extensions/extension_shelf.h"
#include "chrome/browser/views/frame/browser_view.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/devtools_messages.h"
#include "chrome/common/extensions/extension_error_reporter.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/ui_test_utils.h"
#include "net/base/net_util.h"

// Looks for an ExtensionHost whose URL has the given path component (including
// leading slash).  Also verifies that the expected number of hosts are loaded.
static ExtensionHost* FindHostWithPath(ExtensionProcessManager* manager,
                                       const std::string& path,
                                       int expected_hosts) {
  ExtensionHost* host = NULL;
  int num_hosts = 0;
  for (ExtensionProcessManager::const_iterator iter = manager->begin();
       iter != manager->end(); ++iter) {
    if ((*iter)->GetURL().path() == path) {
      EXPECT_FALSE(host);
      host = *iter;
    }
    num_hosts++;
  }
  EXPECT_EQ(expected_hosts, num_hosts);
  EXPECT_TRUE(host);
  return host;
}

// Tests for the experimental timeline extensions API.
IN_PROC_BROWSER_TEST_F(ExtensionDevToolsBrowserTest, TimelineApi) {
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("devtools").AppendASCII("timeline_api")));

  // Get the ExtensionHost that is hosting our background page.
  ExtensionProcessManager* manager =
      browser()->profile()->GetExtensionProcessManager();
  ExtensionHost* host = FindHostWithPath(manager, "/background.html", 1);

  // Grab a handle to the DevToolsManager so we can forward messages to it.
  DevToolsManager* devtools_manager = DevToolsManager::GetInstance();

  // Grab the tab_id of whatever tab happens to be first.
  TabContents* tab_contents = browser()->tabstrip_model()->GetTabContentsAt(0);
  ASSERT_TRUE(tab_contents);
  int tab_id = ExtensionTabUtil::GetTabId(tab_contents);

  // Test setup.
  bool result = false;
  std::wstring register_listeners_js = StringPrintf(L"setListenersOnTab(%d)",
                                                    tab_id);
  ui_test_utils::ExecuteJavaScriptAndExtractBool(
      host->render_view_host(), L"", register_listeners_js, &result);
  EXPECT_TRUE(result);

  // Setting the events should have caused an ExtensionDevToolsBridge to be
  // registered for the tab's RenderViewHost.
  DevToolsClientHost* devtools_client_host =
      devtools_manager->GetDevToolsClientHostFor(
          tab_contents->render_view_host());
  ASSERT_TRUE(devtools_client_host);

  // Test onTabUrlChange event.
  DevToolsClientMsg_RpcMessage tabUrlChangeEventMessage(
      "TimelineAgentClass", "TabUrlChangeEventMessage", "", "", "");
  devtools_client_host->SendMessageToClient(tabUrlChangeEventMessage);
  ui_test_utils::ExecuteJavaScriptAndExtractBool(
      host->render_view_host(),
      L"",
      L"testReceiveTabUrlChangeEvent()",
      &result);
  EXPECT_TRUE(result);

  // Test onPageEvent event.
  result = false;
  DevToolsClientMsg_RpcMessage pageEventMessage(
      "TimelineAgentClass", "PageEventMessage", "", "", "");
  devtools_client_host->SendMessageToClient(pageEventMessage);
  ui_test_utils::ExecuteJavaScriptAndExtractBool(
      host->render_view_host(), L"", L"testReceivePageEvent()", &result);
  EXPECT_TRUE(result);

  // Test onTabClose event.
  result = false;
  devtools_manager->UnregisterDevToolsClientHostFor(
      tab_contents->render_view_host());
  ui_test_utils::ExecuteJavaScriptAndExtractBool(
      host->render_view_host(), L"", L"testReceiveTabCloseEvent()", &result);
  EXPECT_TRUE(result);
}


// Tests that ref counting of listeners from multiple processes works.
IN_PROC_BROWSER_TEST_F(ExtensionDevToolsBrowserTest, ProcessRefCounting) {
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("devtools").AppendASCII("timeline_api")));

  // Get the ExtensionHost that is hosting our background page.
  ExtensionProcessManager* manager =
      browser()->profile()->GetExtensionProcessManager();
  ExtensionHost* host_one = FindHostWithPath(manager, "/background.html", 1);

  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("devtools").AppendASCII("timeline_api_two")));
  ExtensionHost* host_two = FindHostWithPath(manager,
                                             "/background_two.html", 2);

  DevToolsManager* devtools_manager = DevToolsManager::GetInstance();

  // Grab the tab_id of whatever tab happens to be first.
  TabContents* tab_contents = browser()->tabstrip_model()->GetTabContentsAt(0);
  ASSERT_TRUE(tab_contents);
  int tab_id = ExtensionTabUtil::GetTabId(tab_contents);

  // Test setup.
  bool result = false;
  std::wstring register_listeners_js = StringPrintf(L"setListenersOnTab(%d)",
                                                    tab_id);
  ui_test_utils::ExecuteJavaScriptAndExtractBool(
      host_one->render_view_host(), L"", register_listeners_js, &result);
  EXPECT_TRUE(result);

  // Setting the event listeners should have caused an ExtensionDevToolsBridge
  // to be registered for the tab's RenderViewHost.
  ASSERT_TRUE(devtools_manager->GetDevToolsClientHostFor(
      tab_contents->render_view_host()));

  // Register listeners from the second extension as well.
  std::wstring script = StringPrintf(L"registerListenersForTab(%d)", tab_id);
  ui_test_utils::ExecuteJavaScriptAndExtractBool(
      host_two->render_view_host(), L"", script, &result);
  EXPECT_TRUE(result);

  // Removing the listeners from the first extension should leave the bridge
  // alive.
  result = false;
  ui_test_utils::ExecuteJavaScriptAndExtractBool(
      host_one->render_view_host(), L"", L"unregisterListeners()", &result);
  EXPECT_TRUE(result);
  ASSERT_TRUE(devtools_manager->GetDevToolsClientHostFor(
      tab_contents->render_view_host()));

  // Removing the listeners from the second extension should tear the bridge
  // down.
  result = false;
  ui_test_utils::ExecuteJavaScriptAndExtractBool(
      host_two->render_view_host(), L"", L"unregisterListeners()", &result);
  EXPECT_TRUE(result);
  ASSERT_FALSE(devtools_manager->GetDevToolsClientHostFor(
      tab_contents->render_view_host()));
}
