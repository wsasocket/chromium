// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/nacl/browser/test_nacl_browser_delegate.h"

TestNaClBrowserDelegate::TestNaClBrowserDelegate() {}

TestNaClBrowserDelegate::~TestNaClBrowserDelegate() {}

void TestNaClBrowserDelegate::ShowNaClInfobar(int render_process_id,
                                              int render_view_id,
                                              int error_id) {}

bool TestNaClBrowserDelegate::DialogsAreSuppressed() {
  return false;
}

bool TestNaClBrowserDelegate::GetCacheDirectory(base::FilePath* cache_dir) {
  return false;
}

bool TestNaClBrowserDelegate::GetPluginDirectory(base::FilePath* plugin_dir) {
  return false;
}

bool TestNaClBrowserDelegate::GetPnaclDirectory(base::FilePath* pnacl_dir) {
  return false;
}

bool TestNaClBrowserDelegate::GetUserDirectory(base::FilePath* user_dir) {
  return false;
}

std::string TestNaClBrowserDelegate::GetVersionString() const {
  return std::string();
}

ppapi::host::HostFactory* TestNaClBrowserDelegate::CreatePpapiHostFactory(
    content::BrowserPpapiHost* ppapi_host) {
  return NULL;
}

bool TestNaClBrowserDelegate::MapUrlToLocalFilePath(const GURL& url,
                                                    bool use_blocking_api,
                                                    base::FilePath* file_path) {
  return false;
}

void TestNaClBrowserDelegate::SetDebugPatterns(std::string debug_patterns) {}

bool TestNaClBrowserDelegate::URLMatchesDebugPatterns(
    const GURL& manifest_url) {
  return false;
}

content::BrowserPpapiHost::OnKeepaliveCallback
TestNaClBrowserDelegate::GetOnKeepaliveCallback() {
  return content::BrowserPpapiHost::OnKeepaliveCallback();
}
