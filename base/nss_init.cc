// Copyright (c) 2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/nss_init.h"

#include <nss.h>

// Work around https://bugzilla.mozilla.org/show_bug.cgi?id=455424
// until NSS 3.12.2 comes out and we update to it.
#define Lock FOO_NSS_Lock
#include <secmod.h>
#include <ssl.h>
#undef Lock

#include "base/file_util.h"
#include "base/logging.h"
#include "base/singleton.h"

namespace {

// Load nss's built-in root certs.
// TODO(port): figure out a less hacky way to do this
const char *possible_locations[] = { 
  "libnssckbi.so",
  "/usr/lib32/nss/libnssckbi.so",
  "/usr/lib/nss/libnssckbi.so",
  "/usr/lib32/libnssckbi.so",
  "/usr/lib/libnssckbi.so",
  NULL
};
SECMODModule *InitDefaultRootCerts() {
  int i;
  for (i=0; possible_locations[i]; i++) {
    if (possible_locations[i][0] == '/' && access(possible_locations[i], R_OK)) 
      continue;
    char modparams[1024];
    snprintf(modparams, sizeof(modparams),
            "name=\"Root Certs\" library=\"%s\"\n", possible_locations[i]);
    SECMODModule *root = SECMOD_LoadUserModule(modparams, NULL, PR_FALSE);
    if (root)
      return root;
  }
  // Aw, snap.  Can't find/load root cert shared library.
  // This will make it hard to talk to anybody via https.
  NOTREACHED();
  return NULL;
}

class NSSInitSingleton {
 public:
  NSSInitSingleton() {

    // Initialize without using a persistant database (e.g. ~/.netscape)
    CHECK(NSS_NoDB_Init(".") == SECSuccess);

    root_ = InitDefaultRootCerts();

    NSS_SetDomesticPolicy();

    // Explicitly enable exactly those ciphers with keys of at least 80 bits
    for (int i = 0; i < SSL_NumImplementedCiphers; i++) {
      SSLCipherSuiteInfo info;
      if (SSL_GetCipherSuiteInfo(SSL_ImplementedCiphers[i], &info, 
                                 sizeof(info)) == SECSuccess) {
        SSL_CipherPrefSetDefault(SSL_ImplementedCiphers[i], 
                                 (info.effectiveKeyBits >= 80));
      }
    }

    // Enable SSL
    SSL_OptionSetDefault(SSL_SECURITY, PR_TRUE);

    // All other SSL options are set per-session by SSLClientSocket 
  }

  ~NSSInitSingleton() {
    if (root_) {
      SECMOD_UnloadUserModule(root_);
      SECMOD_DestroyModule(root_);
      root_ = NULL;
    }

    // Have to clear the cache, or NSS_Shutdown fails with SEC_ERROR_BUSY
    SSL_ClearSessionCache();

    SECStatus status = NSS_Shutdown();
    if (status != SECSuccess)
      LOG(ERROR) << "NSS_Shutdown failed, leak?  See "
                    "http://code.google.com/p/chromium/issues/detail?id=4609";
  }
 private:
  SECMODModule *root_;
};

}  // namespace

namespace base {

void EnsureNSSInit() {
  Singleton<NSSInitSingleton>::get();
}

}  // namespace base
