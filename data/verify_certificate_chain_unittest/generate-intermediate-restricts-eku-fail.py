#!/usr/bin/python
# Copyright (c) 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Certificate chain with 1 intermediate and a trusted root. The intermediate
restricts the EKU to clientAuth, and the target has serverAuth +
clientAuth. Verification is expected to fail when requesting serverAuth."""

import common

# Self-signed root certificate (used as trust anchor).
root = common.create_self_signed_root_certificate('Root')

# Intermediate certificate.
intermediate = common.create_intermediate_certificate('Intermediate', root)
intermediate.get_extensions().set_property('extendedKeyUsage',
                                           'clientAuth')

# Target certificate.
target = common.create_end_entity_certificate('Target', intermediate)
target.get_extensions().set_property('extendedKeyUsage',
                                     'serverAuth,clientAuth')

chain = [target, intermediate]
trusted = common.TrustAnchor(root, constrained=False)
time = common.DEFAULT_TIME
key_purpose = common.KEY_PURPOSE_SERVER_AUTH
verify_result = False
errors = """----- Certificate i=1 (CN=Intermediate) -----
ERROR: The extended key usage does not include server auth

"""

common.write_test_file(__doc__, chain, trusted, time, key_purpose,
                       verify_result, errors)
