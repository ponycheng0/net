// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/x509_util_nss.h"

#include "net/cert/scoped_nss_types.h"
#include "net/cert/x509_certificate.h"
#include "net/test/cert_test_util.h"
#include "net/test/test_data_directory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

TEST(X509UtilNSSTest, GetDefaultNickname) {
  base::FilePath certs_dir = GetTestCertsDirectory();

  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "no_subject_common_name_cert.pem"));
  ASSERT_TRUE(test_cert);

  std::string nickname = x509_util::GetDefaultUniqueNickname(
      test_cert->os_cert_handle(), USER_CERT, nullptr /*slot*/);
  EXPECT_EQ(
      "wtc@google.com's COMODO Client Authentication and "
      "Secure Email CA ID",
      nickname);
}

TEST(X509UtilNSSTest, GetCERTNameDisplayName_CN) {
  base::FilePath certs_dir = GetTestCertsDirectory();

  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_TRUE(test_cert);

  std::string name =
      x509_util::GetCERTNameDisplayName(&test_cert->os_cert_handle()->subject);
  EXPECT_EQ("127.0.0.1", name);
  EXPECT_EQ(test_cert->subject().GetDisplayName(), name);
}

TEST(X509UtilNSSTest, GetCERTNameDisplayName_O) {
  base::FilePath certs_dir =
      GetTestNetDataDirectory().AppendASCII("parse_certificate_unittest");

  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "subject_t61string.pem"));
  ASSERT_TRUE(test_cert);

  std::string name =
      x509_util::GetCERTNameDisplayName(&test_cert->os_cert_handle()->subject);
  EXPECT_EQ(
      " !\"#$%&'()*+,-./"
      "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
      "abcdefghijklmnopqrstuvwxyz{|}~"
      " ¡¢£¤¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæç"
      "èéêëìíîïðñòóôõö÷øùúûüýþÿ",
      name);
  EXPECT_EQ(test_cert->subject().GetDisplayName(), name);
}

TEST(X509UtilNSSTest, ParseClientSubjectAltNames) {
  base::FilePath certs_dir = GetTestCertsDirectory();

  // This cert contains one rfc822Name field, and one Microsoft UPN
  // otherName field.
  scoped_refptr<X509Certificate> san_cert =
      ImportCertFromFile(certs_dir, "client_3.pem");
  ASSERT_NE(static_cast<X509Certificate*>(NULL), san_cert.get());

  std::vector<std::string> rfc822_names;
  x509_util::GetRFC822SubjectAltNames(san_cert->os_cert_handle(),
                                      &rfc822_names);
  ASSERT_EQ(1U, rfc822_names.size());
  EXPECT_EQ("santest@example.com", rfc822_names[0]);

  std::vector<std::string> upn_names;
  x509_util::GetUPNSubjectAltNames(san_cert->os_cert_handle(), &upn_names);
  ASSERT_EQ(1U, upn_names.size());
  EXPECT_EQ("santest@ad.corp.example.com", upn_names[0]);
}

}  // namespace net
