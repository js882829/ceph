// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "test/rbd_mirror/test_mock_fixture.h"
#include "cls/rbd/cls_rbd_types.h"
#include "librbd/journal/TypeTraits.h"
#include "tools/rbd_mirror/image_replayer/PrepareLocalImageRequest.h"
#include "test/journal/mock/MockJournaler.h"
#include "test/librados_test_stub/MockTestMemIoCtxImpl.h"
#include "test/librbd/mock/MockImageCtx.h"
#include "test/librbd/mock/MockJournal.h"

namespace librbd {

namespace {

struct MockTestImageCtx : public librbd::MockImageCtx {
  MockTestImageCtx(librbd::ImageCtx &image_ctx)
    : librbd::MockImageCtx(image_ctx) {
  }
};

} // anonymous namespace
} // namespace librbd

// template definitions
#include "tools/rbd_mirror/image_replayer/PrepareLocalImageRequest.cc"

namespace rbd {
namespace mirror {
namespace image_replayer {

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::WithArg;
using ::testing::WithArgs;

class TestMockImageReplayerPrepareLocalImageRequest : public TestMockFixture {
public:
  typedef PrepareLocalImageRequest<librbd::MockTestImageCtx> MockPrepareLocalImageRequest;

  void expect_mirror_image_get_image_id(librados::IoCtx &io_ctx,
                                        const std::string &image_id, int r) {
    bufferlist bl;
    ::encode(image_id, bl);

    EXPECT_CALL(get_mock_io_ctx(io_ctx),
                exec(RBD_MIRRORING, _, StrEq("rbd"), StrEq("mirror_image_get_image_id"), _, _, _))
      .WillOnce(DoAll(WithArg<5>(Invoke([bl](bufferlist *out_bl) {
                                          *out_bl = bl;
                                        })),
                      Return(r)));
  }

  void expect_mirror_image_get(librados::IoCtx &io_ctx,
                               cls::rbd::MirrorImageState state,
                               const std::string &global_id, int r) {
    cls::rbd::MirrorImage mirror_image;
    mirror_image.state = state;
    mirror_image.global_image_id = global_id;

    bufferlist bl;
    ::encode(mirror_image, bl);

    EXPECT_CALL(get_mock_io_ctx(io_ctx),
                exec(RBD_MIRRORING, _, StrEq("rbd"), StrEq("mirror_image_get"), _, _, _))
      .WillOnce(DoAll(WithArg<5>(Invoke([bl](bufferlist *out_bl) {
                                          *out_bl = bl;
                                        })),
                      Return(r)));
  }

  void expect_get_tag_owner(librbd::MockJournal &mock_journal,
                            const std::string &local_image_id,
                            const std::string &tag_owner, int r) {
    EXPECT_CALL(mock_journal, get_tag_owner(local_image_id, _, _, _))
      .WillOnce(WithArgs<1, 3>(Invoke([tag_owner, r](std::string *owner, Context *on_finish) {
                                        *owner = tag_owner;
                                        on_finish->complete(r);
                                      })));
  }

};

TEST_F(TestMockImageReplayerPrepareLocalImageRequest, Success) {
  InSequence seq;
  expect_mirror_image_get_image_id(m_local_io_ctx, "local image id", 0);
  expect_mirror_image_get(m_local_io_ctx, cls::rbd::MIRROR_IMAGE_STATE_ENABLED,
                          "global image id", 0);

  librbd::MockJournal mock_journal;
  expect_get_tag_owner(mock_journal, "local image id", "remote mirror uuid", 0);

  std::string local_image_id;
  std::string tag_owner;
  C_SaferCond ctx;
  auto req = MockPrepareLocalImageRequest::create(m_local_io_ctx,
                                                  "global image id",
                                                  &local_image_id,
                                                  &tag_owner,
                                                  m_threads->work_queue,
                                                  &ctx);
  req->send();

  ASSERT_EQ(0, ctx.wait());
  ASSERT_EQ(std::string("local image id"), local_image_id);
  ASSERT_EQ(std::string("remote mirror uuid"), tag_owner);
}

TEST_F(TestMockImageReplayerPrepareLocalImageRequest, MirrorImageIdDNE) {
  InSequence seq;
  expect_mirror_image_get_image_id(m_local_io_ctx, "", -ENOENT);

  std::string local_image_id;
  std::string tag_owner;
  C_SaferCond ctx;
  auto req = MockPrepareLocalImageRequest::create(m_local_io_ctx,
                                                  "global image id",
                                                  &local_image_id,
                                                  &tag_owner,
                                                  m_threads->work_queue,
                                                  &ctx);
  req->send();

  ASSERT_EQ(-ENOENT, ctx.wait());
}

TEST_F(TestMockImageReplayerPrepareLocalImageRequest, MirrorImageIdError) {
  InSequence seq;
  expect_mirror_image_get_image_id(m_local_io_ctx, "", -EINVAL);

  std::string local_image_id;
  std::string tag_owner;
  C_SaferCond ctx;
  auto req = MockPrepareLocalImageRequest::create(m_local_io_ctx,
                                                  "global image id",
                                                  &local_image_id,
                                                  &tag_owner,
                                                  m_threads->work_queue,
                                                  &ctx);
  req->send();

  ASSERT_EQ(-EINVAL, ctx.wait());
}

TEST_F(TestMockImageReplayerPrepareLocalImageRequest, MirrorImageError) {
  InSequence seq;
  expect_mirror_image_get_image_id(m_local_io_ctx, "local image id", 0);
  expect_mirror_image_get(m_local_io_ctx, cls::rbd::MIRROR_IMAGE_STATE_DISABLED,
                          "", -EINVAL);

  std::string local_image_id;
  std::string tag_owner;
  C_SaferCond ctx;
  auto req = MockPrepareLocalImageRequest::create(m_local_io_ctx,
                                                  "global image id",
                                                  &local_image_id,
                                                  &tag_owner,
                                                  m_threads->work_queue,
                                                  &ctx);
  req->send();

  ASSERT_EQ(-EINVAL, ctx.wait());
}

TEST_F(TestMockImageReplayerPrepareLocalImageRequest, TagOwnerError) {
  InSequence seq;
  expect_mirror_image_get_image_id(m_local_io_ctx, "local image id", 0);
  expect_mirror_image_get(m_local_io_ctx, cls::rbd::MIRROR_IMAGE_STATE_ENABLED,
                          "global image id", 0);

  librbd::MockJournal mock_journal;
  expect_get_tag_owner(mock_journal, "local image id", "remote mirror uuid",
                       -ENOENT);

  std::string local_image_id;
  std::string tag_owner;
  C_SaferCond ctx;
  auto req = MockPrepareLocalImageRequest::create(m_local_io_ctx,
                                                  "global image id",
                                                  &local_image_id,
                                                  &tag_owner,
                                                  m_threads->work_queue,
                                                  &ctx);
  req->send();

  ASSERT_EQ(-ENOENT, ctx.wait());
}

} // namespace image_replayer
} // namespace mirror
} // namespace rbd
