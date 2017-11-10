// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/tls_client_handshaker.h"
#include "net/quic/core/tls_server_handshaker.h"
#include "net/quic/platform/api/quic_test.h"
#include "net/quic/test_tools/crypto_test_utils.h"
#include "net/quic/test_tools/fake_proof_source.h"
#include "net/quic/test_tools/quic_test_utils.h"
#include "net/tools/quic/test_tools/mock_quic_session_visitor.h"

using std::string;

namespace net {
namespace test {
namespace {

using ::testing::_;

class FakeProofVerifier : public ProofVerifier {
 public:
  FakeProofVerifier()
      : verifier_(crypto_test_utils::ProofVerifierForTesting()) {}

  QuicAsyncStatus VerifyProof(
      const string& hostname,
      const uint16_t port,
      const string& server_config,
      QuicTransportVersion quic_version,
      QuicStringPiece chlo_hash,
      const std::vector<string>& certs,
      const string& cert_sct,
      const string& signature,
      const ProofVerifyContext* context,
      string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    return verifier_->VerifyProof(
        hostname, port, server_config, quic_version, chlo_hash, certs, cert_sct,
        signature, context, error_details, details, std::move(callback));
  }

  QuicAsyncStatus VerifyCertChain(
      const string& hostname,
      const std::vector<string>& certs,
      const ProofVerifyContext* context,
      string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    if (!active_) {
      return verifier_->VerifyCertChain(hostname, certs, context, error_details,
                                        details, std::move(callback));
    }
    pending_ops_.push_back(QuicMakeUnique<VerifyChainPendingOp>(
        hostname, certs, context, error_details, details, std::move(callback),
        verifier_.get()));
    return QUIC_PENDING;
  }

  void Activate() { active_ = true; }

  size_t NumPendingCallbacks() const { return pending_ops_.size(); }

  void InvokePendingCallback(size_t n) {
    CHECK(NumPendingCallbacks() > n);
    pending_ops_[n]->Run();
    auto it = pending_ops_.begin() + n;
    pending_ops_.erase(it);
  }

 private:
  class VerifyChainPendingOp {
   public:
    VerifyChainPendingOp(const string& hostname,
                         const std::vector<string>& certs,
                         const ProofVerifyContext* context,
                         string* error_details,
                         std::unique_ptr<ProofVerifyDetails>* details,
                         std::unique_ptr<ProofVerifierCallback> callback,
                         ProofVerifier* delegate)
        : hostname_(hostname),
          certs_(certs),
          context_(context),
          error_details_(error_details),
          details_(details),
          callback_(std::move(callback)),
          delegate_(delegate) {}

    void Run() {
      // FakeProofVerifier depends on crypto_test_utils::ProofVerifierForTesting
      // running synchronously, and passes a null callback.
      QuicAsyncStatus status = delegate_->VerifyCertChain(
          hostname_, certs_, context_, error_details_, details_,
          std::unique_ptr<ProofVerifierCallback>());
      ASSERT_NE(status, QUIC_PENDING);
      callback_->Run(status == QUIC_SUCCESS, *error_details_, details_);
    }

   private:
    string hostname_;
    std::vector<string> certs_;
    const ProofVerifyContext* context_;
    string* error_details_;
    std::unique_ptr<ProofVerifyDetails>* details_;
    std::unique_ptr<ProofVerifierCallback> callback_;
    ProofVerifier* delegate_;
  };

  std::unique_ptr<ProofVerifier> verifier_;
  bool active_ = false;
  std::vector<std::unique_ptr<VerifyChainPendingOp>> pending_ops_;
};

class TestQuicCryptoStream : public QuicCryptoStream {
 public:
  explicit TestQuicCryptoStream(QuicSession* session)
      : QuicCryptoStream(session) {}

  ~TestQuicCryptoStream() override = default;

  virtual TlsHandshaker* handshaker() const = 0;

  bool encryption_established() const override {
    return handshaker()->encryption_established();
  }

  bool handshake_confirmed() const override {
    return handshaker()->handshake_confirmed();
  }

  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override {
    return handshaker()->crypto_negotiated_params();
  }

  CryptoMessageParser* crypto_message_parser() override {
    return handshaker()->crypto_message_parser();
  }

  void WriteCryptoData(const QuicStringPiece& data) override {
    pending_writes_.push_back(string(data));
  }

  const std::vector<string>& pending_writes() { return pending_writes_; }

  // Sends the pending frames to |stream| and clears the array of pending
  // writes.
  void SendFramesToStream(QuicCryptoStream* stream) {
    QUIC_LOG(INFO) << "Sending " << pending_writes_.size() << " frames";
    for (size_t i = 0; i < pending_writes_.size(); ++i) {
      QuicStreamFrame frame(kCryptoStreamId, false, stream->stream_bytes_read(),
                            pending_writes_[i]);
      stream->OnStreamFrame(frame);
    }
    pending_writes_.clear();
  }

 private:
  std::vector<string> pending_writes_;
};

class TestQuicCryptoClientStream : public TestQuicCryptoStream {
 public:
  explicit TestQuicCryptoClientStream(QuicSession* session)
      : TestQuicCryptoStream(session),
        proof_verifier_(new FakeProofVerifier),
        ssl_ctx_(SSL_CTX_new(TLS_with_buffers_method())),
        handshaker_(new TlsClientHandshaker(
            this,
            session,
            QuicServerId("test.example.com", 443),
            proof_verifier_.get(),
            ssl_ctx_.get(),
            crypto_test_utils::ProofVerifyContextForTesting())) {
    SSL_CTX_set_min_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);
  }

  ~TestQuicCryptoClientStream() override = default;

  TlsHandshaker* handshaker() const override { return handshaker_.get(); }

  bool CryptoConnect() { return handshaker_->CryptoConnect(); }

  FakeProofVerifier* GetFakeProofVerifier() const {
    return proof_verifier_.get();
  }

 private:
  std::unique_ptr<FakeProofVerifier> proof_verifier_;
  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
  std::unique_ptr<TlsClientHandshaker> handshaker_;
};

class TestQuicCryptoServerStream : public TestQuicCryptoStream {
 public:
  TestQuicCryptoServerStream(QuicSession* session,
                             FakeProofSource* proof_source)
      : TestQuicCryptoStream(session),
        proof_source_(proof_source),
        ssl_ctx_(SSL_CTX_new(TLS_with_buffers_method())),
        handshaker_(new TlsServerHandshaker(this,
                                            session,
                                            ssl_ctx_.get(),
                                            proof_source_)) {
    SSL_CTX_set_min_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_tlsext_servername_callback(
        ssl_ctx_.get(), TlsServerHandshaker::SelectCertificateCallback);
  }

  ~TestQuicCryptoServerStream() override = default;

  void CancelOutstandingCallbacks() {
    handshaker_->CancelOutstandingCallbacks();
  }

  TlsHandshaker* handshaker() const override { return handshaker_.get(); }

  FakeProofSource* GetFakeProofSource() const { return proof_source_; }

 private:
  FakeProofSource* proof_source_;
  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
  std::unique_ptr<TlsServerHandshaker> handshaker_;
};

void MoveStreamFrames(TestQuicCryptoStream* client,
                      TestQuicCryptoStream* server) {
  while (!client->pending_writes().empty() ||
         !server->pending_writes().empty()) {
    client->SendFramesToStream(server);
    server->SendFramesToStream(client);
  }
}

class TlsHandshakerTest : public QuicTest {
 public:
  TlsHandshakerTest()
      : client_conn_(new MockQuicConnection(&conn_helper_,
                                            &alarm_factory_,
                                            Perspective::IS_CLIENT)),
        server_conn_(new MockQuicConnection(&conn_helper_,
                                            &alarm_factory_,
                                            Perspective::IS_SERVER)),
        client_session_(client_conn_),
        server_session_(server_conn_),
        client_stream_(&client_session_),
        server_stream_(
            new TestQuicCryptoServerStream(&server_session_, &proof_source_)) {
    EXPECT_FALSE(client_stream_.encryption_established());
    EXPECT_FALSE(client_stream_.handshake_confirmed());
    EXPECT_FALSE(server_stream_->encryption_established());
    EXPECT_FALSE(server_stream_->handshake_confirmed());
  }

  MockQuicConnectionHelper conn_helper_;
  MockAlarmFactory alarm_factory_;
  MockQuicConnection* client_conn_;
  MockQuicConnection* server_conn_;
  MockQuicSession client_session_;
  MockQuicSession server_session_;

  TestQuicCryptoClientStream client_stream_;
  FakeProofSource proof_source_;
  std::unique_ptr<TestQuicCryptoServerStream> server_stream_;
};

TEST_F(TlsHandshakerTest, CryptoHandshake) {
  EXPECT_CALL(*client_conn_, CloseConnection(_, _, _)).Times(0);
  EXPECT_CALL(*server_conn_, CloseConnection(_, _, _)).Times(0);
  client_stream_.CryptoConnect();
  MoveStreamFrames(&client_stream_, server_stream_.get());

  // TODO(nharper): Once encryption keys are set, check that
  // encryption_established() is true on the streams.
  EXPECT_TRUE(client_stream_.handshake_confirmed());
  EXPECT_TRUE(server_stream_->handshake_confirmed());
}

TEST_F(TlsHandshakerTest, HandshakeWithAsyncProofSource) {
  EXPECT_CALL(*client_conn_, CloseConnection(_, _, _)).Times(0);
  EXPECT_CALL(*server_conn_, CloseConnection(_, _, _)).Times(0);
  // Enable FakeProofSource to capture call to ComputeTlsSignature and run it
  // asynchronously.
  FakeProofSource* proof_source = server_stream_->GetFakeProofSource();
  proof_source->Activate();

  // Start handshake.
  client_stream_.CryptoConnect();
  MoveStreamFrames(&client_stream_, server_stream_.get());

  ASSERT_EQ(proof_source->NumPendingCallbacks(), 1);
  proof_source->InvokePendingCallback(0);

  MoveStreamFrames(&client_stream_, server_stream_.get());

  // TODO(nharper): Once encryption keys are set, check that
  // encryption_established() is true on the streams.
  EXPECT_TRUE(client_stream_.handshake_confirmed());
  EXPECT_TRUE(server_stream_->handshake_confirmed());
}

TEST_F(TlsHandshakerTest, CancelPendingProofSource) {
  EXPECT_CALL(*client_conn_, CloseConnection(_, _, _)).Times(0);
  EXPECT_CALL(*server_conn_, CloseConnection(_, _, _)).Times(0);
  // Enable FakeProofSource to capture call to ComputeTlsSignature and run it
  // asynchronously.
  FakeProofSource* proof_source = server_stream_->GetFakeProofSource();
  proof_source->Activate();

  // Start handshake.
  client_stream_.CryptoConnect();
  MoveStreamFrames(&client_stream_, server_stream_.get());

  ASSERT_EQ(proof_source->NumPendingCallbacks(), 1);
  server_stream_.reset();

  proof_source->InvokePendingCallback(0);
}

TEST_F(TlsHandshakerTest, DISABLED_HandshakeWithAsyncProofVerifier) {
  EXPECT_CALL(*client_conn_, CloseConnection(_, _, _)).Times(0);
  EXPECT_CALL(*server_conn_, CloseConnection(_, _, _)).Times(0);
  // Enable FakeProofVerifier to capture call to VerifyCertChain and run it
  // asynchronously.
  FakeProofVerifier* proof_verifier = client_stream_.GetFakeProofVerifier();
  proof_verifier->Activate();

  // Start handshake.
  client_stream_.CryptoConnect();
  MoveStreamFrames(&client_stream_, server_stream_.get());

  ASSERT_EQ(proof_verifier->NumPendingCallbacks(), 1u);
  proof_verifier->InvokePendingCallback(0);

  MoveStreamFrames(&client_stream_, server_stream_.get());

  // TODO(nharper): Once encryption keys are set, check that
  // encryption_established() is true on the streams.
  EXPECT_TRUE(client_stream_.handshake_confirmed());
  EXPECT_TRUE(server_stream_->handshake_confirmed());
}

TEST_F(TlsHandshakerTest, ClientConnectionClosedOnTlsAlert) {
  // Have client send ClientHello.
  client_stream_.CryptoConnect();
  EXPECT_CALL(*client_conn_, CloseConnection(QUIC_HANDSHAKE_FAILED, _, _));

  // Send fake "internal_error" fatal TLS alert from server to client.
  char alert_msg[] = {
      // TLSPlaintext struct:
      21,          // ContentType alert
      0x03, 0x01,  // ProcotolVersion legacy_record_version
      0, 2,        // uint16 length
      // Alert struct (TLSPlaintext fragment):
      2,   // AlertLevel fatal
      80,  // AlertDescription internal_error
  };
  QuicStreamFrame alert(kCryptoStreamId, false,
                        client_stream_.stream_bytes_read(),
                        QuicStringPiece(alert_msg, arraysize(alert_msg)));
  client_stream_.OnStreamFrame(alert);

  EXPECT_FALSE(client_stream_.handshake_confirmed());
}

TEST_F(TlsHandshakerTest, ServerConnectionClosedOnTlsAlert) {
  EXPECT_CALL(*server_conn_, CloseConnection(QUIC_HANDSHAKE_FAILED, _, _));

  // Send fake "internal_error" fatal TLS alert from client to server.
  char alert_msg[] = {
      // TLSPlaintext struct:
      21,          // ContentType alert
      0x03, 0x01,  // ProcotolVersion legacy_record_version
      0, 2,        // uint16 length
      // Alert struct (TLSPlaintext fragment):
      2,   // AlertLevel fatal
      80,  // AlertDescription internal_error
  };
  QuicStreamFrame alert(kCryptoStreamId, false,
                        server_stream_->stream_bytes_read(),
                        QuicStringPiece(alert_msg, arraysize(alert_msg)));
  server_stream_->OnStreamFrame(alert);

  EXPECT_FALSE(server_stream_->handshake_confirmed());
}

}  // namespace
}  // namespace test
}  // namespace net