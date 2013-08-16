// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/command_line.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/browser_close_manager.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/ui/app_modal_dialogs/javascript_app_modal_dialog.h"
#include "chrome/browser/ui/app_modal_dialogs/native_app_modal_dialog.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace {

class AppModalDialogObserver {
 public:
  AppModalDialogObserver() {}

  void Start() {
    observer_.reset(new content::WindowedNotificationObserver(
        chrome::NOTIFICATION_APP_MODAL_DIALOG_SHOWN,
        content::NotificationService::AllSources()));
  }

  void AcceptClose() {
    NativeAppModalDialog* dialog = GetNextDialog();
    ASSERT_TRUE(dialog);
    dialog->AcceptAppModalDialog();
  }

  void CancelClose() {
    NativeAppModalDialog* dialog = GetNextDialog();
    ASSERT_TRUE(dialog);
    dialog->CancelAppModalDialog();
  }

 private:
  NativeAppModalDialog* GetNextDialog() {
    DCHECK(observer_);
    observer_->Wait();
    if (observer_->source() == content::NotificationService::AllSources())
      return NULL;

    AppModalDialog* dialog =
        content::Source<AppModalDialog>(observer_->source()).ptr();
    EXPECT_TRUE(dialog->IsJavaScriptModalDialog());
    JavaScriptAppModalDialog* js_dialog =
        static_cast<JavaScriptAppModalDialog*>(dialog);
    observer_.reset(new content::WindowedNotificationObserver(
        chrome::NOTIFICATION_APP_MODAL_DIALOG_SHOWN,
        content::NotificationService::AllSources()));
    return js_dialog->native_dialog();
  }

  scoped_ptr<content::WindowedNotificationObserver> observer_;

  DISALLOW_COPY_AND_ASSIGN(AppModalDialogObserver);
};

class RepeatedNotificationObserver : public content::NotificationObserver {
 public:
  explicit RepeatedNotificationObserver(int type, int count)
      : num_outstanding_(count), running_(false) {
    registrar_.Add(this, type, content::NotificationService::AllSources());
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    ASSERT_GT(num_outstanding_, 0);
    if (!--num_outstanding_ && running_) {
      content::BrowserThread::PostTask(
          content::BrowserThread::UI, FROM_HERE, run_loop_.QuitClosure());
    }
  }

  void Wait() {
    if (num_outstanding_ <= 0)
      return;

    running_ = true;
    run_loop_.Run();
    running_ = false;
  }

 private:
  int num_outstanding_;
  content::NotificationRegistrar registrar_;
  bool running_;
  base::RunLoop run_loop_;

  DISALLOW_COPY_AND_ASSIGN(RepeatedNotificationObserver);
};

}  // namespace

class BrowserCloseManagerBrowserTest
    : public InProcessBrowserTest,
      public testing::WithParamInterface<bool> {
 protected:
  virtual void SetUpOnMainThread() OVERRIDE {
    InProcessBrowserTest::SetUpOnMainThread();
    SessionStartupPref::SetStartupPref(
        browser()->profile(), SessionStartupPref(SessionStartupPref::LAST));
    browsers_.push_back(browser());
    dialogs_.Start();
  }

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitch(switches::kEnableBatchedShutdown);
    if (GetParam())
      command_line->AppendSwitch(switches::kEnableFastUnload);
  }

  std::vector<Browser*> browsers_;
  AppModalDialogObserver dialogs_;
};

IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest, TestSingleTabShutdown) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/beforeunload.html")));
  RepeatedNotificationObserver cancel_observer(
      chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 1);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  cancel_observer.Wait();
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  EXPECT_EQ(1, browser()->tab_strip_model()->count());

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 1);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest,
                       TestShutdownMoreThanOnce) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/beforeunload.html")));
  RepeatedNotificationObserver cancel_observer(
      chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 1);
  chrome::CloseAllBrowsers();
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  cancel_observer.Wait();
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  EXPECT_EQ(1, browser()->tab_strip_model()->count());

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 1);
  chrome::CloseAllBrowsers();
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest, PRE_TestSessionRestore) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/beforeunload.html")));
  AddBlankTabAndShow(browser());
  ASSERT_NO_FATAL_FAILURE(
      ui_test_utils::NavigateToURL(browser(), GURL(chrome::kChromeUIAboutURL)));
  RepeatedNotificationObserver cancel_observer(
      chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 1);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  cancel_observer.Wait();
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());

  browser()->tab_strip_model()
      ->CloseWebContentsAt(1, TabStripModel::CLOSE_USER_GESTURE);
  content::TestNavigationObserver navigation_observer(
      browser()->tab_strip_model()->GetActiveWebContents(), 1);
  ASSERT_NO_FATAL_FAILURE(
      NavigateToURLWithDisposition(browser(),
                                   GURL(chrome::kChromeUIVersionURL),
                                   CURRENT_TAB,
                                   ui_test_utils::BROWSER_TEST_NONE));
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  navigation_observer.Wait();

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 1);
  chrome::CloseAllBrowsers();
  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

// Test that the tab closed after the aborted shutdown attempt is not re-opened
// when restoring the session.
IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest, TestSessionRestore) {
  // The testing framework launches Chrome with about:blank as args.
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_EQ(GURL(chrome::kChromeUIVersionURL),
            browser()->tab_strip_model()->GetWebContentsAt(0)->GetURL());
  EXPECT_EQ(GURL("about:blank"),
            browser()->tab_strip_model()->GetWebContentsAt(1)->GetURL());
}

// Test that browser windows are only closed if all browsers are ready to close
// and that all beforeunload dialogs are shown again after a cancel.
IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest, TestMultipleWindows) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  browsers_.push_back(CreateBrowser(browser()->profile()));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload.html")));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[1], embedded_test_server()->GetURL("/beforeunload.html")));

  // Cancel shutdown on the first beforeunload event.
  {
    RepeatedNotificationObserver cancel_observer(
        chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 1);
    chrome::CloseAllBrowsers();
    ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
    cancel_observer.Wait();
  }
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  EXPECT_EQ(1, browsers_[0]->tab_strip_model()->count());
  EXPECT_EQ(1, browsers_[1]->tab_strip_model()->count());

  // Cancel shutdown on the second beforeunload event.
  {
    RepeatedNotificationObserver cancel_observer(
        chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 2);
    chrome::CloseAllBrowsers();
    ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
    ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
    cancel_observer.Wait();
  }
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  EXPECT_EQ(1, browsers_[0]->tab_strip_model()->count());
  EXPECT_EQ(1, browsers_[1]->tab_strip_model()->count());

  // Allow shutdown for both beforeunload events.
  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 2);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

// Test that tabs in the same window with a beforeunload event that hangs are
// treated the same as the user accepting the close, but do not close the tab
// early.
IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest,
                       TestHangInBeforeUnloadMultipleTabs) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload_hang.html")));
  AddBlankTabAndShow(browsers_[0]);
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload.html")));
  AddBlankTabAndShow(browsers_[0]);
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload_hang.html")));

  RepeatedNotificationObserver cancel_observer(
      chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 1);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  cancel_observer.Wait();
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  // All tabs should still be open.
  EXPECT_EQ(3, browsers_[0]->tab_strip_model()->count());

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 1);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

// Test that tabs in different windows with a beforeunload event that hangs are
// treated the same as the user accepting the close, but do not close the tab
// early.
IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest,
                       TestHangInBeforeUnloadMultipleWindows) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  browsers_.push_back(CreateBrowser(browser()->profile()));
  browsers_.push_back(CreateBrowser(browser()->profile()));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload_hang.html")));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[1], embedded_test_server()->GetURL("/beforeunload.html")));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[2], embedded_test_server()->GetURL("/beforeunload_hang.html")));

  RepeatedNotificationObserver cancel_observer(
      chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 2);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  cancel_observer.Wait();
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  // All windows should still be open.
  EXPECT_EQ(1, browsers_[0]->tab_strip_model()->count());
  EXPECT_EQ(1, browsers_[1]->tab_strip_model()->count());
  EXPECT_EQ(1, browsers_[2]->tab_strip_model()->count());

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 3);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

// Test that a window created during shutdown is closed.
IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest,
                       TestAddWindowDuringShutdown) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload.html")));

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 2);
  chrome::CloseAllBrowsers();
  browsers_.push_back(CreateBrowser(browser()->profile()));
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

// Test that a window created during shutdown with a beforeunload handler can
// cancel the shutdown.
IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest,
                       TestAddWindowWithBeforeUnloadDuringShutdown) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload.html")));

  RepeatedNotificationObserver cancel_observer(
      chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 2);
  chrome::CloseAllBrowsers();
  browsers_.push_back(CreateBrowser(browser()->profile()));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[1], embedded_test_server()->GetURL("/beforeunload.html")));
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  cancel_observer.Wait();
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  EXPECT_EQ(1, browsers_[0]->tab_strip_model()->count());
  EXPECT_EQ(1, browsers_[1]->tab_strip_model()->count());

  // Allow shutdown for both beforeunload dialogs.
  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 2);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

// Test that tabs added during shutdown are closed.
IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest,
                       TestAddTabDuringShutdown) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  browsers_.push_back(CreateBrowser(browser()->profile()));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload.html")));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[1], embedded_test_server()->GetURL("/beforeunload.html")));

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 2);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  AddBlankTabAndShow(browsers_[0]);
  AddBlankTabAndShow(browsers_[1]);
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

// Test that tabs created during shutdown with beforeunload handlers can cancel
// the shutdown.
IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest,
                       TestAddTabWithBeforeUnloadDuringShutdown) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  browsers_.push_back(CreateBrowser(browser()->profile()));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload.html")));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[1], embedded_test_server()->GetURL("/beforeunload.html")));
  RepeatedNotificationObserver cancel_observer(
      chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 2);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  AddBlankTabAndShow(browsers_[0]);
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload.html")));
  AddBlankTabAndShow(browsers_[1]);
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[1], embedded_test_server()->GetURL("/beforeunload.html")));
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  cancel_observer.Wait();
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  EXPECT_EQ(2, browsers_[0]->tab_strip_model()->count());
  EXPECT_EQ(2, browsers_[1]->tab_strip_model()->count());

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 2);
  chrome::CloseAllBrowsers();
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());

  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest,
                       TestCloseTabDuringShutdown) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload.html")));
  RepeatedNotificationObserver cancel_observer(
      chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 1);
  chrome::CloseAllBrowsers();

  browsers_.push_back(CreateBrowser(browser()->profile()));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[1], embedded_test_server()->GetURL("/beforeunload.html")));
  browsers_[1]->tab_strip_model()->CloseAllTabs();
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  cancel_observer.Wait();
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  EXPECT_EQ(1, browsers_[0]->tab_strip_model()->count());
  EXPECT_EQ(1, browsers_[1]->tab_strip_model()->count());

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 2);
  chrome::CloseAllBrowsers();
  browsers_[1]->tab_strip_model()->CloseAllTabs();
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());

  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest,
                       TestOpenAndCloseWindowDuringShutdown) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload.html")));
  RepeatedNotificationObserver cancel_observer(
      chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 2);
  chrome::CloseAllBrowsers();

  browsers_.push_back(CreateBrowser(browser()->profile()));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[1], embedded_test_server()->GetURL("/beforeunload.html")));
  ASSERT_FALSE(browsers_[1]->ShouldCloseWindow());
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  cancel_observer.Wait();
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  EXPECT_EQ(1, browsers_[0]->tab_strip_model()->count());
  EXPECT_EQ(1, browsers_[1]->tab_strip_model()->count());

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 2);
  chrome::CloseAllBrowsers();
  ASSERT_FALSE(browsers_[1]->ShouldCloseWindow());
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());

  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

IN_PROC_BROWSER_TEST_P(BrowserCloseManagerBrowserTest,
                       TestCloseWindowDuringShutdown) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[0], embedded_test_server()->GetURL("/beforeunload.html")));
  browsers_.push_back(CreateBrowser(browser()->profile()));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(
      browsers_[1], embedded_test_server()->GetURL("/beforeunload.html")));
  RepeatedNotificationObserver cancel_observer(
      chrome::NOTIFICATION_BROWSER_CLOSE_CANCELLED, 1);
  chrome::CloseAllBrowsers();

  ASSERT_FALSE(browsers_[0]->ShouldCloseWindow());
  ASSERT_NO_FATAL_FAILURE(dialogs_.CancelClose());
  cancel_observer.Wait();
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  EXPECT_EQ(1, browsers_[0]->tab_strip_model()->count());
  EXPECT_EQ(1, browsers_[1]->tab_strip_model()->count());

  RepeatedNotificationObserver close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED, 2);
  chrome::CloseAllBrowsers();
  ASSERT_FALSE(browsers_[0]->ShouldCloseWindow());
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());
  ASSERT_NO_FATAL_FAILURE(dialogs_.AcceptClose());

  close_observer.Wait();
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
  EXPECT_TRUE(chrome::BrowserIterator().done());
}

INSTANTIATE_TEST_CASE_P(BrowserCloseManagerBrowserTest,
                        BrowserCloseManagerBrowserTest,
                        testing::Bool());
