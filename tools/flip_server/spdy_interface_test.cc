// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/flip_server/spdy_interface.h"

#include <list>
#include <memory>

#include "base/strings/string_piece.h"
#include "net/spdy/buffered_spdy_framer.h"
#include "net/tools/balsa/balsa_enums.h"
#include "net/tools/balsa/balsa_headers.h"
#include "net/tools/flip_server/flip_config.h"
#include "net/tools/flip_server/flip_test_utils.h"
#include "net/tools/flip_server/mem_cache.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

using ::base::StringPiece;
using ::testing::_;
using ::testing::InSequence;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::Values;

inline bool operator==(StringPiece x,
                       const SpdyHeaderBlock::StringPieceProxy& y) {
  return x == y.operator StringPiece();
}

namespace {

struct StringSaver {
 public:
  StringSaver() : data(NULL), size(0) {}
  void Save() { string = std::string(data, size); }

  const char* data;
  size_t size;
  std::string string;
};

class SpdyFramerVisitor : public BufferedSpdyFramerVisitorInterface {
 public:
  virtual ~SpdyFramerVisitor() {}
  MOCK_METHOD1(OnError, void(SpdyFramer::SpdyError));
  MOCK_METHOD2(OnStreamError, void(SpdyStreamId, const std::string&));
  MOCK_METHOD6(OnSynStream,
               void(SpdyStreamId,
                    SpdyStreamId,
                    SpdyPriority,
                    bool,
                    bool,
                    const SpdyHeaderBlock&));
  MOCK_METHOD3(OnSynReply, void(SpdyStreamId, bool, const SpdyHeaderBlock&));
  MOCK_METHOD7(OnHeaders,
               void(SpdyStreamId stream_id,
                    bool has_priority,
                    int weight,
                    SpdyStreamId parent_stream_id,
                    bool exclusive,
                    bool fin,
                    const SpdyHeaderBlock& headers));
  MOCK_METHOD3(OnDataFrameHeader, void(SpdyStreamId, size_t, bool));
  MOCK_METHOD3(OnStreamFrameData, void(SpdyStreamId, const char*, size_t));
  MOCK_METHOD1(OnStreamEnd, void(SpdyStreamId));
  MOCK_METHOD2(OnStreamPadding, void(SpdyStreamId, size_t));
  MOCK_METHOD1(OnHeaderFrameStart,
               SpdyHeadersHandlerInterface*(SpdyStreamId stream_id));
  MOCK_METHOD2(OnHeaderFrameEnd,
               void(SpdyStreamId stream_id, bool end_headers));
  MOCK_METHOD1(OnSettings, void(bool clear_persisted));
  MOCK_METHOD3(OnSetting, void(SpdySettingsIds, uint8_t, uint32_t));
  MOCK_METHOD2(OnPing, void(SpdyPingId unique_id, bool is_ack));
  MOCK_METHOD2(OnRstStream, void(SpdyStreamId, SpdyRstStreamStatus));
  MOCK_METHOD3(OnGoAway, void(SpdyStreamId, SpdyGoAwayStatus, StringPiece));
  MOCK_METHOD2(OnWindowUpdate, void(SpdyStreamId, int));
  MOCK_METHOD3(OnPushPromise,
               void(SpdyStreamId, SpdyStreamId, const SpdyHeaderBlock&));
  MOCK_METHOD2(OnUnknownFrame, bool(SpdyStreamId stream_id, int frame_type));
};

class FakeSMConnection : public SMConnection {
 public:
  FakeSMConnection(EpollServer* epoll_server,
                   SSLState* ssl_state,
                   MemoryCache* memory_cache,
                   FlipAcceptor* acceptor,
                   std::string log_prefix)
      : SMConnection(epoll_server,
                     ssl_state,
                     memory_cache,
                     acceptor,
                     log_prefix) {}

  MOCK_METHOD0(Cleanup, void());
  MOCK_METHOD8(InitSMConnection,
               void(SMConnectionPoolInterface*,
                    SMInterface*,
                    EpollServer*,
                    int,
                    std::string,
                    std::string,
                    std::string,
                    bool));
};

// This class is almost SpdySM, except one function.
// This class is the test target of tests in this file.
class TestSpdySM : public SpdySM {
 public:
  virtual ~TestSpdySM() {}
  TestSpdySM(SMConnection* connection,
             SMInterface* sm_http_interface,
             EpollServer* epoll_server,
             MemoryCache* memory_cache,
             FlipAcceptor* acceptor,
             SpdyMajorVersion version)
      : SpdySM(connection,
               sm_http_interface,
               epoll_server,
               memory_cache,
               acceptor,
               version) {}

  MOCK_METHOD2(FindOrMakeNewSMConnectionInterface,
               SMInterface*(const std::string&, const std::string&));
};

class SpdySMTestBase : public ::testing::TestWithParam<SpdyMajorVersion> {
 public:
  explicit SpdySMTestBase(FlipHandlerType type) {
    SSLState* ssl_state = NULL;
    mock_another_interface_.reset(new MockSMInterface);
    memory_cache_.reset(new MemoryCache);
    acceptor_.reset(new FlipAcceptor(type,
                                     "127.0.0.1",
                                     "8941",
                                     "ssl_cert_filename",
                                     "ssl_key_filename",
                                     "127.0.0.1",
                                     "8942",
                                     "127.0.0.1",
                                     "8943",
                                     1,
                                     0,
                                     true,
                                     1,
                                     false,
                                     true,
                                     NULL));
    epoll_server_.reset(new EpollServer);
    connection_.reset(new FakeSMConnection(epoll_server_.get(),
                                           ssl_state,
                                           memory_cache_.get(),
                                           acceptor_.get(),
                                           "log_prefix"));

    interface_.reset(new TestSpdySM(connection_.get(),
                                    mock_another_interface_.get(),
                                    epoll_server_.get(),
                                    memory_cache_.get(),
                                    acceptor_.get(),
                                    GetParam()));

    spdy_framer_.reset(new BufferedSpdyFramer(GetParam()));
    spdy_framer_visitor_.reset(new SpdyFramerVisitor);
    spdy_framer_->set_visitor(spdy_framer_visitor_.get());
  }

  virtual ~SpdySMTestBase() {
    if (acceptor_->listen_fd_ >= 0) {
      epoll_server_->UnregisterFD(acceptor_->listen_fd_);
      close(acceptor_->listen_fd_);
      acceptor_->listen_fd_ = -1;
    }
    OutputList& output_list = *connection_->output_list();
    for (OutputList::const_iterator i = output_list.begin();
         i != output_list.end();
         ++i) {
      delete *i;
    }
    output_list.clear();
  }

  bool HasStream(uint32_t stream_id) {
    return interface_->output_ordering().ExistsInPriorityMaps(stream_id);
  }

 protected:
  std::unique_ptr<MockSMInterface> mock_another_interface_;
  std::unique_ptr<MemoryCache> memory_cache_;
  std::unique_ptr<FlipAcceptor> acceptor_;
  std::unique_ptr<EpollServer> epoll_server_;
  std::unique_ptr<FakeSMConnection> connection_;
  std::unique_ptr<TestSpdySM> interface_;
  std::unique_ptr<BufferedSpdyFramer> spdy_framer_;
  std::unique_ptr<SpdyFramerVisitor> spdy_framer_visitor_;
};

class SpdySMProxyTest : public SpdySMTestBase {
 public:
  SpdySMProxyTest() : SpdySMTestBase(FLIP_HANDLER_PROXY) {}
  virtual ~SpdySMProxyTest() {}
};

class SpdySMServerTest : public SpdySMTestBase {
 public:
  SpdySMServerTest() : SpdySMTestBase(FLIP_HANDLER_SPDY_SERVER) {}
  virtual ~SpdySMServerTest() {}
};

INSTANTIATE_TEST_CASE_P(SpdySMProxyTest, SpdySMProxyTest, Values(SPDY3, HTTP2));
INSTANTIATE_TEST_CASE_P(SpdySMServerTest, SpdySMServerTest, Values(HTTP2));

TEST_P(SpdySMProxyTest, InitSMConnection) {
  {
    InSequence s;
    EXPECT_CALL(*connection_, InitSMConnection(_, _, _, _, _, _, _, _));
  }
  interface_->InitSMConnection(
      NULL, NULL, epoll_server_.get(), -1, "", "", "", false);
}

TEST_P(SpdySMProxyTest, OnStreamFrameData) {
  BufferedSpdyFramerVisitorInterface* visitor = interface_.get();
  std::unique_ptr<MockSMInterface> mock_interface(new MockSMInterface);
  uint32_t stream_id = 92;
  uint32_t associated_id = 43;
  SpdyHeaderBlock block;
  testing::MockFunction<void(int)> checkpoint;  // NOLINT

  std::unique_ptr<SpdySerializedFrame> frame(
      spdy_framer_->CreatePingFrame(12, false));
  block[":method"] = "GET";
  block[":host"] = "www.example.com";
  block[":path"] = "/path";
  block[":scheme"] = "http";
  block["foo"] = "bar";
  {
    InSequence s;
    EXPECT_CALL(*interface_,
                FindOrMakeNewSMConnectionInterface(_, _))
        .WillOnce(Return(mock_interface.get()));
    EXPECT_CALL(*mock_interface, SetStreamID(stream_id));
    EXPECT_CALL(*mock_interface, ProcessWriteInput(_, _)).Times(1);
    EXPECT_CALL(checkpoint, Call(0));
    EXPECT_CALL(*mock_interface,
                ProcessWriteInput(frame->data(), frame->size())).Times(1);
  }

  visitor->OnSynStream(stream_id, associated_id, 0, false, false, block);
  checkpoint.Call(0);
  visitor->OnStreamFrameData(stream_id, frame->data(), frame->size());
}

TEST_P(SpdySMProxyTest, OnRstStream) {
  BufferedSpdyFramerVisitorInterface* visitor = interface_.get();
  uint32_t stream_id = 82;
  MemCacheIter mci;
  mci.stream_id = stream_id;

  {
    BalsaHeaders headers;
    std::string filename = "foobar";
    memory_cache_->InsertFile(&headers, filename, "");
    mci.file_data = memory_cache_->GetFileData(filename);
  }

  interface_->AddToOutputOrder(mci);
  ASSERT_TRUE(HasStream(stream_id));
  visitor->OnRstStream(stream_id, RST_STREAM_INVALID);
  ASSERT_FALSE(HasStream(stream_id));
}

TEST_P(SpdySMProxyTest, ProcessReadInput) {
  ASSERT_EQ(SpdyFramer::SPDY_READY_FOR_FRAME,
            interface_->spdy_framer()->state());
  interface_->ProcessReadInput("", 1);
  ASSERT_EQ(SpdyFramer::SPDY_READING_COMMON_HEADER,
            interface_->spdy_framer()->state());
}

TEST_P(SpdySMProxyTest, ResetForNewConnection) {
  uint32_t stream_id = 13;
  MemCacheIter mci;
  mci.stream_id = stream_id;
  // incomplete input
  const char input[] = {'\0', '\0', '\0'};

  {
    BalsaHeaders headers;
    std::string filename = "foobar";
    memory_cache_->InsertFile(&headers, filename, "");
    mci.file_data = memory_cache_->GetFileData(filename);
  }

  interface_->AddToOutputOrder(mci);
  ASSERT_TRUE(HasStream(stream_id));
  interface_->ProcessReadInput(input, sizeof(input));
  ASSERT_NE(SpdyFramer::SPDY_READY_FOR_FRAME,
            interface_->spdy_framer()->state());

  interface_->ResetForNewConnection();
  ASSERT_FALSE(HasStream(stream_id));
  ASSERT_TRUE(interface_->spdy_framer() == NULL);
}

TEST_P(SpdySMProxyTest, CreateFramer) {
  interface_->ResetForNewConnection();
  interface_->CreateFramer(SPDY3);
  ASSERT_TRUE(interface_->spdy_framer() != NULL);
  ASSERT_EQ(interface_->spdy_version(), SPDY3);

  interface_->ResetForNewConnection();
  interface_->CreateFramer(HTTP2);
  ASSERT_TRUE(interface_->spdy_framer() != NULL);
  ASSERT_EQ(interface_->spdy_version(), HTTP2);
}

TEST_P(SpdySMProxyTest, PostAcceptHook) {
  interface_->PostAcceptHook();

  ASSERT_EQ(1u, connection_->output_list()->size());
  std::list<DataFrame*>::const_iterator i = connection_->output_list()->begin();
  DataFrame* df = *i++;

  {
    InSequence s;
    EXPECT_CALL(*spdy_framer_visitor_, OnSettings(false));
    EXPECT_CALL(*spdy_framer_visitor_,
                OnSetting(SETTINGS_MAX_CONCURRENT_STREAMS, 0u, 100u));
  }
  spdy_framer_->ProcessInput(df->data, df->size);
}

TEST_P(SpdySMProxyTest, NewStream) {
  // TODO(yhirano): SpdySM::NewStream leads to crash when
  // acceptor_->flip_handler_type_ != FLIP_HANDLER_SPDY_SERVER.
  // It should be fixed though I don't know the solution now.
}

TEST_P(SpdySMProxyTest, AddToOutputOrder) {
  uint32_t stream_id = 13;
  MemCacheIter mci;
  mci.stream_id = stream_id;

  {
    BalsaHeaders headers;
    std::string filename = "foobar";
    memory_cache_->InsertFile(&headers, filename, "");
    mci.file_data = memory_cache_->GetFileData(filename);
  }

  interface_->AddToOutputOrder(mci);
  ASSERT_TRUE(HasStream(stream_id));
}

TEST_P(SpdySMProxyTest, SendErrorNotFound) {
  uint32_t stream_id = 82;
  SpdyHeaderBlock actual_header_block;
  const char* actual_data;
  size_t actual_size;
  testing::MockFunction<void(int)> checkpoint;  // NOLINT

  interface_->SendErrorNotFound(stream_id);

  ASSERT_EQ(2u, connection_->output_list()->size());

  {
    InSequence s;
    if (GetParam() < HTTP2) {
      EXPECT_CALL(*spdy_framer_visitor_,
                  OnSynReply(stream_id, false, _))
          .WillOnce(SaveArg<2>(&actual_header_block));
    } else {
      EXPECT_CALL(*spdy_framer_visitor_,
                  OnHeaders(stream_id, /*has_priority=*/false, _, _, _,
                            /*fin=*/false, _))
          .WillOnce(SaveArg<6>(&actual_header_block));
    }
    EXPECT_CALL(checkpoint, Call(0));
    EXPECT_CALL(*spdy_framer_visitor_,
                OnDataFrameHeader(stream_id, _, true));
    EXPECT_CALL(*spdy_framer_visitor_, OnStreamFrameData(stream_id, _, _))
        .Times(1)
        .WillOnce(DoAll(SaveArg<1>(&actual_data), SaveArg<2>(&actual_size)));
    EXPECT_CALL(*spdy_framer_visitor_, OnStreamFrameData(stream_id, NULL, 0))
        .Times(1);
  }

  std::list<DataFrame*>::const_iterator i = connection_->output_list()->begin();
  DataFrame* df = *i++;
  spdy_framer_->ProcessInput(df->data, df->size);
  checkpoint.Call(0);
  df = *i++;
  spdy_framer_->ProcessInput(df->data, df->size);

  ASSERT_EQ(2, spdy_framer_->frames_received());
  ASSERT_EQ(2u, actual_header_block.size());
  ASSERT_EQ("404 Not Found", actual_header_block[":status"]);
  ASSERT_EQ("HTTP/1.1", actual_header_block[":version"]);
  ASSERT_EQ("wtf?", StringPiece(actual_data, actual_size));
}

TEST_P(SpdySMProxyTest, SendSynStream) {
  uint32_t stream_id = 82;
  BalsaHeaders headers;
  SpdyHeaderBlock actual_header_block;
  headers.AppendHeader("key1", "value1");
  headers.AppendHeader("Host", "www.example.com");
  headers.SetRequestFirstlineFromStringPieces("GET", "/path", "HTTP/1.1");

  interface_->SendSynStream(stream_id, headers);

  ASSERT_EQ(1u, connection_->output_list()->size());
  std::list<DataFrame*>::const_iterator i = connection_->output_list()->begin();
  DataFrame* df = *i++;

  {
    InSequence s;
    EXPECT_CALL(*spdy_framer_visitor_,
                OnSynStream(stream_id, 0, _, false, false, _))
        .WillOnce(SaveArg<5>(&actual_header_block));
  }

  spdy_framer_->ProcessInput(df->data, df->size);
  ASSERT_EQ(1, spdy_framer_->frames_received());
  ASSERT_EQ(5u, actual_header_block.size());
  ASSERT_EQ("GET", actual_header_block[":method"]);
  ASSERT_EQ("HTTP/1.1", actual_header_block[":version"]);
  ASSERT_EQ("/path", actual_header_block[":path"]);
  ASSERT_EQ("www.example.com", actual_header_block[":host"]);
  ASSERT_EQ("value1", actual_header_block["key1"]);
}

TEST_P(SpdySMProxyTest, SendSynReply) {
  uint32_t stream_id = 82;
  BalsaHeaders headers;
  SpdyHeaderBlock actual_header_block;
  headers.AppendHeader("key1", "value1");
  headers.SetResponseFirstlineFromStringPieces("HTTP/1.1", "200", "OK");

  interface_->SendSynReply(stream_id, headers);

  ASSERT_EQ(1u, connection_->output_list()->size());
  std::list<DataFrame*>::const_iterator i = connection_->output_list()->begin();
  DataFrame* df = *i++;

  {
    InSequence s;
    if (GetParam() < HTTP2) {
      EXPECT_CALL(*spdy_framer_visitor_, OnSynReply(stream_id, false, _))
          .WillOnce(SaveArg<2>(&actual_header_block));
    } else {
      EXPECT_CALL(*spdy_framer_visitor_,
                  OnHeaders(stream_id, /*has_priority=*/false, _, _, _,
                            /*fin=*/false, _))
          .WillOnce(SaveArg<6>(&actual_header_block));
    }
  }

  spdy_framer_->ProcessInput(df->data, df->size);
  ASSERT_EQ(1, spdy_framer_->frames_received());
  ASSERT_EQ(3u, actual_header_block.size());
  ASSERT_EQ("200 OK", actual_header_block[":status"]);
  ASSERT_EQ("HTTP/1.1", actual_header_block[":version"]);
  ASSERT_EQ("value1", actual_header_block["key1"]);
}

TEST_P(SpdySMProxyTest, SendDataFrame) {
  uint32_t stream_id = 133;
  SpdyDataFlags flags = DATA_FLAG_NONE;
  const char* actual_data;
  size_t actual_size;

  interface_->SendDataFrame(stream_id, "hello", 5, flags, true);

  ASSERT_EQ(1u, connection_->output_list()->size());
  std::list<DataFrame*>::const_iterator i = connection_->output_list()->begin();
  DataFrame* df = *i++;

  {
    InSequence s;
    EXPECT_CALL(*spdy_framer_visitor_,
                OnDataFrameHeader(stream_id, _, false));
    EXPECT_CALL(*spdy_framer_visitor_, OnStreamFrameData(stream_id, _, _))
        .WillOnce(DoAll(SaveArg<1>(&actual_data), SaveArg<2>(&actual_size)));
  }

  spdy_framer_->ProcessInput(df->data, df->size);
  ASSERT_EQ(1, spdy_framer_->frames_received());
  ASSERT_EQ("hello", StringPiece(actual_data, actual_size));
}

TEST_P(SpdySMProxyTest, SendLongDataFrame) {
  uint32_t stream_id = 133;
  SpdyDataFlags flags = DATA_FLAG_NONE;
  const char* actual_data;
  size_t actual_size;

  std::string data = std::string(kSpdySegmentSize, 'a') +
                     std::string(kSpdySegmentSize, 'b') + "c";
  interface_->SendDataFrame(stream_id, data.data(), data.size(), flags, true);

  {
    InSequence s;
    for (int i = 0; i < 3; ++i) {
        EXPECT_CALL(*spdy_framer_visitor_,
                    OnDataFrameHeader(stream_id, _, false));
        EXPECT_CALL(*spdy_framer_visitor_, OnStreamFrameData(stream_id, _, _))
            .WillOnce(
                DoAll(SaveArg<1>(&actual_data), SaveArg<2>(&actual_size)));
    }
  }

  ASSERT_EQ(3u, connection_->output_list()->size());
  std::list<DataFrame*>::const_iterator i = connection_->output_list()->begin();
  DataFrame* df = *i++;
  spdy_framer_->ProcessInput(df->data, df->size);
  ASSERT_EQ(std::string(kSpdySegmentSize, 'a'),
            StringPiece(actual_data, actual_size));

  df = *i++;
  spdy_framer_->ProcessInput(df->data, df->size);
  ASSERT_EQ(std::string(kSpdySegmentSize, 'b'),
            StringPiece(actual_data, actual_size));

  df = *i++;
  spdy_framer_->ProcessInput(df->data, df->size);
  ASSERT_EQ("c", StringPiece(actual_data, actual_size));
}

TEST_P(SpdySMServerTest, OnSynStream) {
  BufferedSpdyFramerVisitorInterface* visitor = interface_.get();
  uint32_t stream_id = 82;
  SpdyHeaderBlock spdy_headers;
  spdy_headers["url"] = "http://www.example.com/path";
  spdy_headers["method"] = "GET";
  spdy_headers["scheme"] = "http";
  spdy_headers["version"] = "HTTP/1.1";

  {
    BalsaHeaders headers;
    memory_cache_->InsertFile(&headers, "GET_/path", "");
  }
  visitor->OnSynStream(stream_id, 0, 0, true, true, spdy_headers);
  ASSERT_TRUE(HasStream(stream_id));
}

TEST_P(SpdySMServerTest, NewStream) {
  uint32_t stream_id = 13;
  std::string filename = "foobar";

  {
    BalsaHeaders headers;
    memory_cache_->InsertFile(&headers, filename, "");
  }

  interface_->NewStream(stream_id, 0, filename);
  ASSERT_TRUE(HasStream(stream_id));
}

TEST_P(SpdySMServerTest, NewStreamError) {
  uint32_t stream_id = 82;
  SpdyHeaderBlock actual_header_block;
  const char* actual_data;
  size_t actual_size;
  testing::MockFunction<void(int)> checkpoint;  // NOLINT

  interface_->NewStream(stream_id, 0, "nonexistingfile");

  ASSERT_EQ(2u, connection_->output_list()->size());

  {
    InSequence s;
    if (GetParam() < HTTP2) {
      EXPECT_CALL(*spdy_framer_visitor_, OnSynReply(stream_id, false, _))
          .WillOnce(SaveArg<2>(&actual_header_block));
    } else {
      EXPECT_CALL(*spdy_framer_visitor_,
                  OnHeaders(stream_id, /*has_priority=*/false, _, _, _,
                            /*fin=*/false, _))
          .WillOnce(SaveArg<6>(&actual_header_block));
    }
    EXPECT_CALL(checkpoint, Call(0));
    EXPECT_CALL(*spdy_framer_visitor_,
                OnDataFrameHeader(stream_id, _, true));
    EXPECT_CALL(*spdy_framer_visitor_, OnStreamFrameData(stream_id, _, _))
        .Times(1)
        .WillOnce(DoAll(SaveArg<1>(&actual_data), SaveArg<2>(&actual_size)));
    EXPECT_CALL(*spdy_framer_visitor_, OnStreamFrameData(stream_id, NULL, 0))
        .Times(1);
  }

  std::list<DataFrame*>::const_iterator i = connection_->output_list()->begin();
  DataFrame* df = *i++;
  spdy_framer_->ProcessInput(df->data, df->size);
  checkpoint.Call(0);
  df = *i++;
  spdy_framer_->ProcessInput(df->data, df->size);

  ASSERT_EQ(2, spdy_framer_->frames_received());
  ASSERT_EQ(2u, actual_header_block.size());
  ASSERT_EQ("404 Not Found", actual_header_block["status"]);
  ASSERT_EQ("HTTP/1.1", actual_header_block["version"]);
  ASSERT_EQ("wtf?", StringPiece(actual_data, actual_size));
}

}  // namespace

}  // namespace net
