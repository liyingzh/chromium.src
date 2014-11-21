// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/logging_win.h"
#include "components/browser_watcher/exit_code_watcher_win.h"
#include "components/browser_watcher/watcher_main_api_win.h"

namespace {

// Use the same log facility as Chrome for convenience.
// {7FE69228-633E-4f06-80C1-527FEA23E3A7}
const GUID kChromeWatcherTraceProviderName = {
    0x7fe69228, 0x633e, 0x4f06,
        { 0x80, 0xc1, 0x52, 0x7f, 0xea, 0x23, 0xe3, 0xa7 } };

}  // namespace

// The main entry point to the watcher, declared as extern "C" to avoid name
// mangling.
extern "C" int WatcherMain(const base::char16* registry_path) {
  // The exit manager is in charge of calling the dtors of singletons.
  base::AtExitManager exit_manager;
  // Initialize the commandline singleton from the environment.
  base::CommandLine::Init(0, NULL);

  logging::LogEventProvider::Initialize(kChromeWatcherTraceProviderName);

  // Arrange to be shut down as late as possible, as we want to outlive
  // chrome.exe in order to report its exit status.
  // TODO(siggi): Does this (windowless) process need to register a console
  //     handler too, in order to get notice of logoff events?
  ::SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);

  browser_watcher::ExitCodeWatcher exit_code_watcher(registry_path);
  int ret = 1;
  // Attempt to wait on our parent process, and record its exit status.
  if (exit_code_watcher.ParseArguments(
        *base::CommandLine::ForCurrentProcess())) {
    // Wait on the process.
    exit_code_watcher.WaitForExit();
    ret = 0;
  }

  // Wind logging down.
  logging::LogEventProvider::Uninitialize();

  return ret;
}

namespace {

void CompileAsserts() {
  // Make sure the exported function's type matches the typedef.
  browser_watcher::WatcherMainFunction fn = &WatcherMain;
}

}  // namespace
