/*
 * Copyright (C) 2015, 2016 Igalia S.L.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <WPE/WebKit.h>
#include <WPE/WebKit/WKCookieManagerSoup.h>

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <vector>
#include <glib.h>
#include <locale.h>
#include <initializer_list>

static GMainLoop* loop;

static const WKPageNavigationClientV0 s_navigationClient = {
    { 0, nullptr },
    // decidePolicyForNavigationAction
    [](WKPageRef, WKNavigationActionRef, WKFramePolicyListenerRef listener, WKTypeRef, const void*) {
        WKFramePolicyListenerUse(listener);
    },
    // decidePolicyForNavigationResponse
    [](WKPageRef, WKNavigationResponseRef response, WKFramePolicyListenerRef listener, WKTypeRef, const void*) {
        bool canShowMIMEType = WKNavigationResponseCanShowMIMEType(response);

        if (canShowMIMEType)
            WKFramePolicyListenerUse(listener);
        else
            WKFramePolicyListenerIgnore(listener);
    },
    nullptr, // decidePolicyForPluginLoad
    nullptr, // didStartProvisionalNavigation
    nullptr, // didReceiveServerRedirectForProvisionalNavigation
    nullptr, // didFailProvisionalNavigation
    nullptr, // didCommitNavigation
    nullptr, // didFinishNavigation
    nullptr, // didFailNavigation
    nullptr, // didFailProvisionalLoadInSubframe
    // didFinishDocumentLoad
    [](WKPageRef page, WKNavigationRef, WKTypeRef, const void*) {
        WKStringRef messageName = WKStringCreateWithUTF8CString("Hello");
        WKMutableArrayRef messageBody = WKMutableArrayCreate();

        for (auto& item : { "Test1", "Test2", "Test3" }) {
            WKStringRef itemString = WKStringCreateWithUTF8CString(item);
            WKArrayAppendItem(messageBody, itemString);
            WKRelease(itemString);
        }

        fprintf(stderr, "[WPELauncher] Hello InjectedBundle ...\n");
        WKPagePostMessageToInjectedBundle(page, messageName, messageBody);
        WKRelease(messageBody);
        WKRelease(messageName);
    },
    nullptr, // didSameDocumentNavigation
    nullptr, // renderingProgressDidChange
    nullptr, // canAuthenticateAgainstProtectionSpace
    nullptr, // didReceiveAuthenticationChallenge
    // webProcessDidCrash
    [](WKPageRef, const void*)
    {
        fprintf(stderr, "Web process crashed\n");
        g_main_loop_quit(loop);
    },
    nullptr, // copyWebCryptoMasterKey
    nullptr, // didBeginNavigationGesture
    nullptr, // willEndNavigationGesture
    nullptr, // didEndNavigationGesture
    nullptr, // didRemoveNavigationGestureSnapshot
};

static const WKContextClientV2 s_contextClient = {
    { 2, nullptr },
    nullptr, // plugInAutoStartOriginHashesChanged
    // networkProcessDidCrash
    [](WKContextRef, const void*)
    {
        fprintf(stderr, "Network process crashed\n");
        g_main_loop_quit(loop);
    },
    nullptr, // plugInInformationBecameAvailable
    nullptr, // (*copyWebCryptoMasterKey_unavailable)(void)
    // databaseProcessDidCrash
    [](WKContextRef, const void*)
    {
        fprintf(stderr, "Database process crashed\n");
        g_main_loop_quit(loop);
    },
};

static const WKViewClientV0 s_viewClient = {
    { 0, nullptr },
    // frameDisplayed
    [](WKViewRef, const void*) {
        static unsigned s_frameCount = 0;
        static gint64 lastDumpTime = g_get_monotonic_time();

        if (!g_getenv("WPE_DISPLAY_FPS"))
          return;

        ++s_frameCount;
        gint64 time = g_get_monotonic_time();
        if (time - lastDumpTime >= 5 * G_USEC_PER_SEC) {
            fprintf(stderr, "[WPELauncher] %.2f FPS\n",
                s_frameCount * G_USEC_PER_SEC * 1.0 / (time - lastDumpTime));
            s_frameCount = 0;
            lastDumpTime = time;
        }
    },
};

WKStringRef createPath(int mode, ...)
{
    va_list args;
    std::vector<gchar*> path;
    gchar* c;

    va_start(args, mode);
    do {
        c = va_arg(args, gchar*);
        path.push_back(c);
    } while (c);
    va_end(args);

    gchar* p = g_build_filenamev(path.data());

    g_mkdir_with_parents(p, mode);
    auto s = WKStringCreateWithUTF8CString(p);
    g_free(p);

    return s;
}

int main(int argc, char* argv[])
{
    setlocale(LC_ALL, "");

    loop = g_main_loop_new(nullptr, FALSE);

    auto contextConfiguration = WKContextConfigurationCreate();
    auto injectedBundlePath = WKStringCreateWithUTF8CString(PREFIX "/lib/libWPEInjectedBundle.so");
    WKContextConfigurationSetInjectedBundlePath(contextConfiguration, injectedBundlePath);

    WKContextConfigurationSetLocalStorageDirectory(contextConfiguration,
            createPath(0700, g_get_user_cache_dir(), "wpe", "local-storage", nullptr));

    WKContextConfigurationSetDiskCacheDirectory(contextConfiguration,
            createPath(0700, g_get_user_cache_dir(), "wpe", "disk-cache", nullptr));

    WKContextConfigurationSetIndexedDBDatabaseDirectory(contextConfiguration,
            createPath(0700, g_get_user_cache_dir(), "wpe", "index-db", nullptr));

    WKRelease(injectedBundlePath);

    WKContextRef context = WKContextCreateWithConfiguration(contextConfiguration);
    WKContextSetClient(context, &s_contextClient.base);
    WKRelease(contextConfiguration);

    auto pageGroupIdentifier = WKStringCreateWithUTF8CString("WPEPageGroup");
    auto pageGroup = WKPageGroupCreateWithIdentifier(pageGroupIdentifier);
    WKRelease(pageGroupIdentifier);

    auto preferences = WKPreferencesCreate();
    // Allow mixed content.
    WKPreferencesSetAllowRunningOfInsecureContent(preferences, true);
    WKPreferencesSetAllowDisplayOfInsecureContent(preferences, true);
    WKPreferencesSetWebSecurityEnabled(preferences, false);

    // By default allow console log messages to system console reporting.
    if (!g_getenv("WPE_SHELL_DISABLE_CONSOLE_LOG"))
      WKPreferencesSetLogsPageMessagesToSystemConsoleEnabled(preferences, true);

    WKPageGroupSetPreferences(pageGroup, preferences);

    auto pageConfiguration  = WKPageConfigurationCreate();
    WKPageConfigurationSetContext(pageConfiguration, context);
    WKPageConfigurationSetPageGroup(pageConfiguration, pageGroup);
    WKPreferencesSetFullScreenEnabled(preferences, true);

    if (!!g_getenv("WPE_SHELL_COOKIE_STORAGE")) {
      gchar *cookieDatabasePath = g_build_filename(g_get_user_cache_dir(), "cookies.db", nullptr);
      auto path = WKStringCreateWithUTF8CString(cookieDatabasePath);
      g_free(cookieDatabasePath);
      auto cookieManager = WKContextGetCookieManager(context);
      WKCookieManagerSetCookiePersistentStorage(cookieManager, path, kWKCookieStorageTypeSQLite);
    }

    auto view = WKViewCreate(pageConfiguration);
    WKViewSetViewClient(view, &s_viewClient.base);

    auto page = WKViewGetPage(view);
    WKPageSetPageNavigationClient(page, &s_navigationClient.base);

    const char* url = "http://youtube.com/tv";
    if (argc > 1)
        url = argv[1];

    auto shellURL = WKURLCreateWithUTF8CString(url);
    WKPageLoadURL(page, shellURL);
    WKRelease(shellURL);

    g_main_loop_run(loop);

    WKRelease(view);
    WKRelease(pageConfiguration);
    WKRelease(pageGroup);
    WKRelease(context);
    WKRelease(preferences);
    g_main_loop_unref(loop);
    return 0;
}
