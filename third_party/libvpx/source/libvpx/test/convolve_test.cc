/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <string.h>

#include "third_party/googletest/src/include/gtest/gtest.h"

#include "./vp9_rtcd.h"
#include "./vpx_config.h"
#include "./vpx_dsp_rtcd.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"
#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_filter.h"
#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx_dsp/vpx_filter.h"
#include "vpx_mem/vpx_mem.h"
#include "vpx_ports/mem.h"

namespace {

static const unsigned int kMaxDimension = 64;

typedef void (*ConvolveFunc)(const uint8_t *src, ptrdiff_t src_stride,
                             uint8_t *dst, ptrdiff_t dst_stride,
                             const int16_t *filter_x, int filter_x_stride,
                             const int16_t *filter_y, int filter_y_stride,
                             int w, int h);

typedef void (*WrapperFilterBlock2d8Func)(
    const uint8_t *src_ptr, const unsigned int src_stride,
    const int16_t *hfilter, const int16_t *vfilter, uint8_t *dst_ptr,
    unsigned int dst_stride, unsigned int output_width,
    unsigned int output_height, int use_highbd);

struct ConvolveFunctions {
  ConvolveFunctions(ConvolveFunc copy, ConvolveFunc avg, ConvolveFunc h8,
                    ConvolveFunc h8_avg, ConvolveFunc v8, ConvolveFunc v8_avg,
                    ConvolveFunc hv8, ConvolveFunc hv8_avg, ConvolveFunc sh8,
                    ConvolveFunc sh8_avg, ConvolveFunc sv8,
                    ConvolveFunc sv8_avg, ConvolveFunc shv8,
                    ConvolveFunc shv8_avg, int bd)
      : use_highbd_(bd) {
    copy_[0] = copy;
    copy_[1] = avg;
    h8_[0] = h8;
    h8_[1] = h8_avg;
    v8_[0] = v8;
    v8_[1] = v8_avg;
    hv8_[0] = hv8;
    hv8_[1] = hv8_avg;
    sh8_[0] = sh8;
    sh8_[1] = sh8_avg;
    sv8_[0] = sv8;
    sv8_[1] = sv8_avg;
    shv8_[0] = shv8;
    shv8_[1] = shv8_avg;
  }

  ConvolveFunc copy_[2];
  ConvolveFunc h8_[2];
  ConvolveFunc v8_[2];
  ConvolveFunc hv8_[2];
  ConvolveFunc sh8_[2];   // scaled horiz
  ConvolveFunc sv8_[2];   // scaled vert
  ConvolveFunc shv8_[2];  // scaled horiz/vert
  int use_highbd_;  // 0 if high bitdepth not used, else the actual bit depth.
};

typedef std::tr1::tuple<int, int, const ConvolveFunctions *> ConvolveParam;

#define ALL_SIZES(convolve_fn)                                            \
  make_tuple(4, 4, &convolve_fn), make_tuple(8, 4, &convolve_fn),         \
      make_tuple(4, 8, &convolve_fn), make_tuple(8, 8, &convolve_fn),     \
      make_tuple(16, 8, &convolve_fn), make_tuple(8, 16, &convolve_fn),   \
      make_tuple(16, 16, &convolve_fn), make_tuple(32, 16, &convolve_fn), \
      make_tuple(16, 32, &convolve_fn), make_tuple(32, 32, &convolve_fn), \
      make_tuple(64, 32, &convolve_fn), make_tuple(32, 64, &convolve_fn), \
      make_tuple(64, 64, &convolve_fn)

// Reference 8-tap subpixel filter, slightly modified to fit into this test.
#define VP9_FILTER_WEIGHT 128
#define VP9_FILTER_SHIFT 7
uint8_t clip_pixel(int x) { return x < 0 ? 0 : x > 255 ? 255 : x; }

void filter_block2d_8_c(const uint8_t *src_ptr, const unsigned int src_stride,
                        const int16_t *hfilter, const int16_t *vfilter,
                        uint8_t *dst_ptr, unsigned int dst_stride,
                        unsigned int output_width, unsigned int output_height) {
  // Between passes, we use an intermediate buffer whose height is extended to
  // have enough horizontally filtered values as input for the vertical pass.
  // This buffer is allocated to be big enough for the largest block type we
  // support.
  const int kInterp_Extend = 4;
  const unsigned int intermediate_height =
      (kInterp_Extend - 1) + output_height + kInterp_Extend;
  unsigned int i, j;

  // Size of intermediate_buffer is max_intermediate_height * filter_max_width,
  // where max_intermediate_height = (kInterp_Extend - 1) + filter_max_height
  //                                 + kInterp_Extend
  //                               = 3 + 16 + 4
  //                               = 23
  // and filter_max_width          = 16
  //
  uint8_t intermediate_buffer[71 * kMaxDimension];
  const int intermediate_next_stride =
      1 - static_cast<int>(intermediate_height * output_width);

  // Horizontal pass (src -> transposed intermediate).
  uint8_t *output_ptr = intermediate_buffer;
  const int src_next_row_stride = src_stride - output_width;
  src_ptr -= (kInterp_Extend - 1) * src_stride + (kInterp_Extend - 1);
  for (i = 0; i < intermediate_height; ++i) {
    for (j = 0; j < output_width; ++j) {
      // Apply filter...
      const int temp = (src_ptr[0] * hfilter[0]) + (src_ptr[1] * hfilter[1]) +
                       (src_ptr[2] * hfilter[2]) + (src_ptr[3] * hfilter[3]) +
                       (src_ptr[4] * hfilter[4]) + (src_ptr[5] * hfilter[5]) +
                       (src_ptr[6] * hfilter[6]) + (src_ptr[7] * hfilter[7]) +
                       (VP9_FILTER_WEIGHT >> 1);  // Rounding

      // Normalize back to 0-255...
      *output_ptr = clip_pixel(temp >> VP9_FILTER_SHIFT);
      ++src_ptr;
      output_ptr += intermediate_height;
    }
    src_ptr += src_next_row_stride;
    output_ptr += intermediate_next_stride;
  }

  // Vertical pass (transposed intermediate -> dst).
  src_ptr = intermediate_buffer;
  const int dst_next_row_stride = dst_stride - output_width;
  for (i = 0; i < output_height; ++i) {
    for (j = 0; j < output_width; ++j) {
      // Apply filter...
      const int temp = (src_ptr[0] * vfilter[0]) + (src_ptr[1] * vfilter[1]) +
                       (src_ptr[2] * vfilter[2]) + (src_ptr[3] * vfilter[3]) +
                       (src_ptr[4] * vfilter[4]) + (src_ptr[5] * vfilter[5]) +
                       (src_ptr[6] * vfilter[6]) + (src_ptr[7] * vfilter[7]) +
                       (VP9_FILTER_WEIGHT >> 1);  // Rounding

      // Normalize back to 0-255...
      *dst_ptr++ = clip_pixel(temp >> VP9_FILTER_SHIFT);
      src_ptr += intermediate_height;
    }
    src_ptr += intermediate_next_stride;
    dst_ptr += dst_next_row_stride;
  }
}

void block2d_average_c(uint8_t *src, unsigned int src_stride,
                       uint8_t *output_ptr, unsigned int output_stride,
                       unsigned int output_width, unsigned int output_height) {
  unsigned int i, j;
  for (i = 0; i < output_height; ++i) {
    for (j = 0; j < output_width; ++j) {
      output_ptr[j] = (output_ptr[j] + src[i * src_stride + j] + 1) >> 1;
    }
    output_ptr += output_stride;
  }
}

void filter_average_block2d_8_c(const uint8_t *src_ptr,
                                const unsigned int src_stride,
                                const int16_t *hfilter, const int16_t *vfilter,
                                uint8_t *dst_ptr, unsigned int dst_stride,
                                unsigned int output_width,
                                unsigned int output_height) {
  uint8_t tmp[kMaxDimension * kMaxDimension];

  assert(output_width <= kMaxDimension);
  assert(output_height <= kMaxDimension);
  filter_block2d_8_c(src_ptr, src_stride, hfilter, vfilter, tmp, 64,
                     output_width, output_height);
  block2d_average_c(tmp, 64, dst_ptr, dst_stride, output_width, output_height);
}

#if CONFIG_VP9_HIGHBITDEPTH
void highbd_filter_block2d_8_c(const uint16_t *src_ptr,
                               const unsigned int src_stride,
                               const int16_t *hfilter, const int16_t *vfilter,
                               uint16_t *dst_ptr, unsigned int dst_stride,
                               unsigned int output_width,
                               unsigned int output_height, int bd) {
  // Between passes, we use an intermediate buffer whose height is extended to
  // have enough horizontally filtered values as input for the vertical pass.
  // This buffer is allocated to be big enough for the largest block type we
  // support.
  const int kInterp_Extend = 4;
  const unsigned int intermediate_height =
      (kInterp_Extend - 1) + output_height + kInterp_Extend;

  /* Size of intermediate_buffer is max_intermediate_height * filter_max_width,
   * where max_intermediate_height = (kInterp_Extend - 1) + filter_max_height
   *                                 + kInterp_Extend
   *                               = 3 + 16 + 4
   *                               = 23
   * and filter_max_width = 16
   */
  uint16_t intermediate_buffer[71 * kMaxDimension];
  const int intermediate_next_stride =
      1 - static_cast<int>(intermediate_height * output_width);

  // Horizontal pass (src -> transposed intermediate).
  {
    uint16_t *output_ptr = intermediate_buffer;
    const int src_next_row_stride = src_stride - output_width;
    unsigned int i, j;
    src_ptr -= (kInterp_Extend - 1) * src_stride + (kInterp_Extend - 1);
    for (i = 0; i < intermediate_height; ++i) {
      for (j = 0; j < output_width; ++j) {
        // Apply filter...
        const int temp = (src_ptr[0] * hfilter[0]) + (src_ptr[1] * hfilter[1]) +
                         (src_ptr[2] * hfilter[2]) + (src_ptr[3] * hfilter[3]) +
                         (src_ptr[4] * hfilter[4]) + (src_ptr[5] * hfilter[5]) +
                         (src_ptr[6] * hfilter[6]) + (src_ptr[7] * hfilter[7]) +
                         (VP9_FILTER_WEIGHT >> 1);  // Rounding

        // Normalize back to 0-255...
        *output_ptr = clip_pixel_highbd(temp >> VP9_FILTER_SHIFT, bd);
        ++src_ptr;
        output_ptr += intermediate_height;
      }
      src_ptr += src_next_row_stride;
      output_ptr += intermediate_next_stride;
    }
  }

  // Vertical pass (transposed intermediate -> dst).
  {
    uint16_t *src_ptr = intermediate_buffer;
    const int dst_next_row_stride = dst_stride - output_width;
    unsigned int i, j;
    for (i = 0; i < output_height; ++i) {
      for (j = 0; j < output_width; ++j) {
        // Apply filter...
        const int temp = (src_ptr[0] * vfilter[0]) + (src_ptr[1] * vfilter[1]) +
                         (src_ptr[2] * vfilter[2]) + (src_ptr[3] * vfilter[3]) +
                         (src_ptr[4] * vfilter[4]) + (src_ptr[5] * vfilter[5]) +
                         (src_ptr[6] * vfilter[6]) + (src_ptr[7] * vfilter[7]) +
                         (VP9_FILTER_WEIGHT >> 1);  // Rounding

        // Normalize back to 0-255...
        *dst_ptr++ = clip_pixel_highbd(temp >> VP9_FILTER_SHIFT, bd);
        src_ptr += intermediate_height;
      }
      src_ptr += intermediate_next_stride;
      dst_ptr += dst_next_row_stride;
    }
  }
}

void highbd_block2d_average_c(uint16_t *src, unsigned int src_stride,
                              uint16_t *output_ptr, unsigned int output_stride,
                              unsigned int output_width,
                              unsigned int output_height) {
  unsigned int i, j;
  for (i = 0; i < output_height; ++i) {
    for (j = 0; j < output_width; ++j) {
      output_ptr[j] = (output_ptr[j] + src[i * src_stride + j] + 1) >> 1;
    }
    output_ptr += output_stride;
  }
}

void highbd_filter_average_block2d_8_c(
    const uint16_t *src_ptr, const unsigned int src_stride,
    const int16_t *hfilter, const int16_t *vfilter, uint16_t *dst_ptr,
    unsigned int dst_stride, unsigned int output_width,
    unsigned int output_height, int bd) {
  uint16_t tmp[kMaxDimension * kMaxDimension];

  assert(output_width <= kMaxDimension);
  assert(output_height <= kMaxDimension);
  highbd_filter_block2d_8_c(src_ptr, src_stride, hfilter, vfilter, tmp, 64,
                            output_width, output_height, bd);
  highbd_block2d_average_c(tmp, 64, dst_ptr, dst_stride, output_width,
                           output_height);
}
#endif  // CONFIG_VP9_HIGHBITDEPTH

void wrapper_filter_average_block2d_8_c(
    const uint8_t *src_ptr, const unsigned int src_stride,
    const int16_t *hfilter, const int16_t *vfilter, uint8_t *dst_ptr,
    unsigned int dst_stride, unsigned int output_width,
    unsigned int output_height, int use_highbd) {
#if CONFIG_VP9_HIGHBITDEPTH
  if (use_highbd == 0) {
    filter_average_block2d_8_c(src_ptr, src_stride, hfilter, vfilter, dst_ptr,
                               dst_stride, output_width, output_height);
  } else {
    highbd_filter_average_block2d_8_c(CONVERT_TO_SHORTPTR(src_ptr), src_stride,
                                      hfilter, vfilter,
                                      CONVERT_TO_SHORTPTR(dst_ptr), dst_stride,
                                      output_width, output_height, use_highbd);
  }
#else
  ASSERT_EQ(0, use_highbd);
  filter_average_block2d_8_c(src_ptr, src_stride, hfilter, vfilter, dst_ptr,
                             dst_stride, output_width, output_height);
#endif
}

void wrapper_filter_block2d_8_c(const uint8_t *src_ptr,
                                const unsigned int src_stride,
                                const int16_t *hfilter, const int16_t *vfilter,
                                uint8_t *dst_ptr, unsigned int dst_stride,
                                unsigned int output_width,
                                unsigned int output_height, int use_highbd) {
#if CONFIG_VP9_HIGHBITDEPTH
  if (use_highbd == 0) {
    filter_block2d_8_c(src_ptr, src_stride, hfilter, vfilter, dst_ptr,
                       dst_stride, output_width, output_height);
  } else {
    highbd_filter_block2d_8_c(CONVERT_TO_SHORTPTR(src_ptr), src_stride, hfilter,
                              vfilter, CONVERT_TO_SHORTPTR(dst_ptr), dst_stride,
                              output_width, output_height, use_highbd);
  }
#else
  ASSERT_EQ(0, use_highbd);
  filter_block2d_8_c(src_ptr, src_stride, hfilter, vfilter, dst_ptr, dst_stride,
                     output_width, output_height);
#endif
}

class ConvolveTest : public ::testing::TestWithParam<ConvolveParam> {
 public:
  static void SetUpTestCase() {
    // Force input_ to be unaligned, output to be 16 byte aligned.
    input_ = reinterpret_cast<uint8_t *>(
                 vpx_memalign(kDataAlignment, kInputBufferSize + 1)) +
             1;
    output_ = reinterpret_cast<uint8_t *>(
        vpx_memalign(kDataAlignment, kOutputBufferSize));
    output_ref_ = reinterpret_cast<uint8_t *>(
        vpx_memalign(kDataAlignment, kOutputBufferSize));
#if CONFIG_VP9_HIGHBITDEPTH
    input16_ = reinterpret_cast<uint16_t *>(vpx_memalign(
                   kDataAlignment, (kInputBufferSize + 1) * sizeof(uint16_t))) +
               1;
    output16_ = reinterpret_cast<uint16_t *>(
        vpx_memalign(kDataAlignment, (kOutputBufferSize) * sizeof(uint16_t)));
    output16_ref_ = reinterpret_cast<uint16_t *>(
        vpx_memalign(kDataAlignment, (kOutputBufferSize) * sizeof(uint16_t)));
#endif
  }

  virtual void TearDown() { libvpx_test::ClearSystemState(); }

  static void TearDownTestCase() {
    vpx_free(input_ - 1);
    input_ = NULL;
    vpx_free(output_);
    output_ = NULL;
    vpx_free(output_ref_);
    output_ref_ = NULL;
#if CONFIG_VP9_HIGHBITDEPTH
    vpx_free(input16_ - 1);
    input16_ = NULL;
    vpx_free(output16_);
    output16_ = NULL;
    vpx_free(output16_ref_);
    output16_ref_ = NULL;
#endif
  }

 protected:
  static const int kDataAlignment = 16;
  static const int kOuterBlockSize = 256;
  static const int kInputStride = kOuterBlockSize;
  static const int kOutputStride = kOuterBlockSize;
  static const int kInputBufferSize = kOuterBlockSize * kOuterBlockSize;
  static const int kOutputBufferSize = kOuterBlockSize * kOuterBlockSize;

  int Width() const { return GET_PARAM(0); }
  int Height() const { return GET_PARAM(1); }
  int BorderLeft() const {
    const int center = (kOuterBlockSize - Width()) / 2;
    return (center + (kDataAlignment - 1)) & ~(kDataAlignment - 1);
  }
  int BorderTop() const { return (kOuterBlockSize - Height()) / 2; }

  bool IsIndexInBorder(int i) {
    return (i < BorderTop() * kOuterBlockSize ||
            i >= (BorderTop() + Height()) * kOuterBlockSize ||
            i % kOuterBlockSize < BorderLeft() ||
            i % kOuterBlockSize >= (BorderLeft() + Width()));
  }

  virtual void SetUp() {
    UUT_ = GET_PARAM(2);
#if CONFIG_VP9_HIGHBITDEPTH
    if (UUT_->use_highbd_ != 0) {
      mask_ = (1 << UUT_->use_highbd_) - 1;
    } else {
      mask_ = 255;
    }
#endif
    /* Set up guard blocks for an inner block centered in the outer block */
    for (int i = 0; i < kOutputBufferSize; ++i) {
      if (IsIndexInBorder(i)) {
        output_[i] = 255;
      } else {
        output_[i] = 0;
      }
    }

    ::libvpx_test::ACMRandom prng;
    for (int i = 0; i < kInputBufferSize; ++i) {
      if (i & 1) {
        input_[i] = 255;
#if CONFIG_VP9_HIGHBITDEPTH
        input16_[i] = mask_;
#endif
      } else {
        input_[i] = prng.Rand8Extremes();
#if CONFIG_VP9_HIGHBITDEPTH
        input16_[i] = prng.Rand16() & mask_;
#endif
      }
    }
  }

  void SetConstantInput(int value) {
    memset(input_, value, kInputBufferSize);
#if CONFIG_VP9_HIGHBITDEPTH
    vpx_memset16(input16_, value, kInputBufferSize);
#endif
  }

  void CopyOutputToRef() {
    memcpy(output_ref_, output_, kOutputBufferSize);
#if CONFIG_VP9_HIGHBITDEPTH
    memcpy(output16_ref_, output16_,
           kOutputBufferSize * sizeof(output16_ref_[0]));
#endif
  }

  void CheckGuardBlocks() {
    for (int i = 0; i < kOutputBufferSize; ++i) {
      if (IsIndexInBorder(i)) EXPECT_EQ(255, output_[i]);
    }
  }

  uint8_t *input() const {
    const int offset = BorderTop() * kOuterBlockSize + BorderLeft();
#if CONFIG_VP9_HIGHBITDEPTH
    if (UUT_->use_highbd_ == 0) {
      return input_ + offset;
    } else {
      return CONVERT_TO_BYTEPTR(input16_) + offset;
    }
#else
    return input_ + offset;
#endif
  }

  uint8_t *output() const {
    const int offset = BorderTop() * kOuterBlockSize + BorderLeft();
#if CONFIG_VP9_HIGHBITDEPTH
    if (UUT_->use_highbd_ == 0) {
      return output_ + offset;
    } else {
      return CONVERT_TO_BYTEPTR(output16_) + offset;
    }
#else
    return output_ + offset;
#endif
  }

  uint8_t *output_ref() const {
    const int offset = BorderTop() * kOuterBlockSize + BorderLeft();
#if CONFIG_VP9_HIGHBITDEPTH
    if (UUT_->use_highbd_ == 0) {
      return output_ref_ + offset;
    } else {
      return CONVERT_TO_BYTEPTR(output16_ref_) + offset;
    }
#else
    return output_ref_ + offset;
#endif
  }

  uint16_t lookup(uint8_t *list, int index) const {
#if CONFIG_VP9_HIGHBITDEPTH
    if (UUT_->use_highbd_ == 0) {
      return list[index];
    } else {
      return CONVERT_TO_SHORTPTR(list)[index];
    }
#else
    return list[index];
#endif
  }

  void assign_val(uint8_t *list, int index, uint16_t val) const {
#if CONFIG_VP9_HIGHBITDEPTH
    if (UUT_->use_highbd_ == 0) {
      list[index] = (uint8_t)val;
    } else {
      CONVERT_TO_SHORTPTR(list)[index] = val;
    }
#else
    list[index] = (uint8_t)val;
#endif
  }

  const ConvolveFunctions *UUT_;
  static uint8_t *input_;
  static uint8_t *output_;
  static uint8_t *output_ref_;
#if CONFIG_VP9_HIGHBITDEPTH
  static uint16_t *input16_;
  static uint16_t *output16_;
  static uint16_t *output16_ref_;
  int mask_;
#endif
};

uint8_t *ConvolveTest::input_ = NULL;
uint8_t *ConvolveTest::output_ = NULL;
uint8_t *ConvolveTest::output_ref_ = NULL;
#if CONFIG_VP9_HIGHBITDEPTH
uint16_t *ConvolveTest::input16_ = NULL;
uint16_t *ConvolveTest::output16_ = NULL;
uint16_t *ConvolveTest::output16_ref_ = NULL;
#endif

TEST_P(ConvolveTest, GuardBlocks) { CheckGuardBlocks(); }

TEST_P(ConvolveTest, Copy) {
  uint8_t *const in = input();
  uint8_t *const out = output();

  ASM_REGISTER_STATE_CHECK(UUT_->copy_[0](in, kInputStride, out, kOutputStride,
                                          NULL, 0, NULL, 0, Width(), Height()));

  CheckGuardBlocks();

  for (int y = 0; y < Height(); ++y) {
    for (int x = 0; x < Width(); ++x)
      ASSERT_EQ(lookup(out, y * kOutputStride + x),
                lookup(in, y * kInputStride + x))
          << "(" << x << "," << y << ")";
  }
}

TEST_P(ConvolveTest, Avg) {
  uint8_t *const in = input();
  uint8_t *const out = output();
  uint8_t *const out_ref = output_ref();
  CopyOutputToRef();

  ASM_REGISTER_STATE_CHECK(UUT_->copy_[1](in, kInputStride, out, kOutputStride,
                                          NULL, 0, NULL, 0, Width(), Height()));

  CheckGuardBlocks();

  for (int y = 0; y < Height(); ++y) {
    for (int x = 0; x < Width(); ++x)
      ASSERT_EQ(lookup(out, y * kOutputStride + x),
                ROUND_POWER_OF_TWO(lookup(in, y * kInputStride + x) +
                                       lookup(out_ref, y * kOutputStride + x),
                                   1))
          << "(" << x << "," << y << ")";
  }
}

TEST_P(ConvolveTest, CopyHoriz) {
  uint8_t *const in = input();
  uint8_t *const out = output();
  DECLARE_ALIGNED(256, const int16_t,
                  filter8[8]) = { 0, 0, 0, 128, 0, 0, 0, 0 };

  ASM_REGISTER_STATE_CHECK(UUT_->sh8_[0](in, kInputStride, out, kOutputStride,
                                         filter8, 16, filter8, 16, Width(),
                                         Height()));

  CheckGuardBlocks();

  for (int y = 0; y < Height(); ++y) {
    for (int x = 0; x < Width(); ++x)
      ASSERT_EQ(lookup(out, y * kOutputStride + x),
                lookup(in, y * kInputStride + x))
          << "(" << x << "," << y << ")";
  }
}

TEST_P(ConvolveTest, CopyVert) {
  uint8_t *const in = input();
  uint8_t *const out = output();
  DECLARE_ALIGNED(256, const int16_t,
                  filter8[8]) = { 0, 0, 0, 128, 0, 0, 0, 0 };

  ASM_REGISTER_STATE_CHECK(UUT_->sv8_[0](in, kInputStride, out, kOutputStride,
                                         filter8, 16, filter8, 16, Width(),
                                         Height()));

  CheckGuardBlocks();

  for (int y = 0; y < Height(); ++y) {
    for (int x = 0; x < Width(); ++x)
      ASSERT_EQ(lookup(out, y * kOutputStride + x),
                lookup(in, y * kInputStride + x))
          << "(" << x << "," << y << ")";
  }
}

TEST_P(ConvolveTest, Copy2D) {
  uint8_t *const in = input();
  uint8_t *const out = output();
  DECLARE_ALIGNED(256, const int16_t,
                  filter8[8]) = { 0, 0, 0, 128, 0, 0, 0, 0 };

  ASM_REGISTER_STATE_CHECK(UUT_->shv8_[0](in, kInputStride, out, kOutputStride,
                                          filter8, 16, filter8, 16, Width(),
                                          Height()));

  CheckGuardBlocks();

  for (int y = 0; y < Height(); ++y) {
    for (int x = 0; x < Width(); ++x)
      ASSERT_EQ(lookup(out, y * kOutputStride + x),
                lookup(in, y * kInputStride + x))
          << "(" << x << "," << y << ")";
  }
}

const int kNumFilterBanks = 4;
const int kNumFilters = 16;

TEST(ConvolveTest, FiltersWontSaturateWhenAddedPairwise) {
  for (int filter_bank = 0; filter_bank < kNumFilterBanks; ++filter_bank) {
    const InterpKernel *filters =
        vp9_filter_kernels[static_cast<INTERP_FILTER>(filter_bank)];
    for (int i = 0; i < kNumFilters; i++) {
      const int p0 = filters[i][0] + filters[i][1];
      const int p1 = filters[i][2] + filters[i][3];
      const int p2 = filters[i][4] + filters[i][5];
      const int p3 = filters[i][6] + filters[i][7];
      EXPECT_LE(p0, 128);
      EXPECT_LE(p1, 128);
      EXPECT_LE(p2, 128);
      EXPECT_LE(p3, 128);
      EXPECT_LE(p0 + p3, 128);
      EXPECT_LE(p0 + p3 + p1, 128);
      EXPECT_LE(p0 + p3 + p1 + p2, 128);
      EXPECT_EQ(p0 + p1 + p2 + p3, 128);
    }
  }
}

const int16_t kInvalidFilter[8] = { 0 };
const WrapperFilterBlock2d8Func wrapper_filter_block2d_8[2] = {
  wrapper_filter_block2d_8_c, wrapper_filter_average_block2d_8_c
};

TEST_P(ConvolveTest, MatchesReferenceSubpixelFilter) {
  for (int i = 0; i < 2; ++i) {
    uint8_t *const in = input();
    uint8_t *const out = output();
#if CONFIG_VP9_HIGHBITDEPTH
    uint8_t ref8[kOutputStride * kMaxDimension];
    uint16_t ref16[kOutputStride * kMaxDimension];
    uint8_t *ref;
    if (UUT_->use_highbd_ == 0) {
      ref = ref8;
    } else {
      ref = CONVERT_TO_BYTEPTR(ref16);
    }
#else
    uint8_t ref[kOutputStride * kMaxDimension];
#endif

    // Populate ref and out with some random data
    ::libvpx_test::ACMRandom prng;
    for (int y = 0; y < Height(); ++y) {
      for (int x = 0; x < Width(); ++x) {
        uint16_t r;
#if CONFIG_VP9_HIGHBITDEPTH
        if (UUT_->use_highbd_ == 0 || UUT_->use_highbd_ == 8) {
          r = prng.Rand8Extremes();
        } else {
          r = prng.Rand16() & mask_;
        }
#else
        r = prng.Rand8Extremes();
#endif

        assign_val(out, y * kOutputStride + x, r);
        assign_val(ref, y * kOutputStride + x, r);
      }
    }

    for (int filter_bank = 0; filter_bank < kNumFilterBanks; ++filter_bank) {
      const InterpKernel *filters =
          vp9_filter_kernels[static_cast<INTERP_FILTER>(filter_bank)];

      for (int filter_x = 0; filter_x < kNumFilters; ++filter_x) {
        for (int filter_y = 0; filter_y < kNumFilters; ++filter_y) {
          wrapper_filter_block2d_8[i](in, kInputStride, filters[filter_x],
                                      filters[filter_y], ref, kOutputStride,
                                      Width(), Height(), UUT_->use_highbd_);

          if (filter_x && filter_y)
            ASM_REGISTER_STATE_CHECK(UUT_->hv8_[i](
                in, kInputStride, out, kOutputStride, filters[filter_x], 16,
                filters[filter_y], 16, Width(), Height()));
          else if (filter_y)
            ASM_REGISTER_STATE_CHECK(UUT_->v8_[i](
                in, kInputStride, out, kOutputStride, kInvalidFilter, 16,
                filters[filter_y], 16, Width(), Height()));
          else if (filter_x)
            ASM_REGISTER_STATE_CHECK(UUT_->h8_[i](
                in, kInputStride, out, kOutputStride, filters[filter_x], 16,
                kInvalidFilter, 16, Width(), Height()));
          else
            ASM_REGISTER_STATE_CHECK(UUT_->copy_[i](
                in, kInputStride, out, kOutputStride, kInvalidFilter, 0,
                kInvalidFilter, 0, Width(), Height()));

          CheckGuardBlocks();

          for (int y = 0; y < Height(); ++y) {
            for (int x = 0; x < Width(); ++x)
              ASSERT_EQ(lookup(ref, y * kOutputStride + x),
                        lookup(out, y * kOutputStride + x))
                  << "mismatch at (" << x << "," << y << "), "
                  << "filters (" << filter_bank << "," << filter_x << ","
                  << filter_y << ")";
          }
        }
      }
    }
  }
}

TEST_P(ConvolveTest, FilterExtremes) {
  uint8_t *const in = input();
  uint8_t *const out = output();
#if CONFIG_VP9_HIGHBITDEPTH
  uint8_t ref8[kOutputStride * kMaxDimension];
  uint16_t ref16[kOutputStride * kMaxDimension];
  uint8_t *ref;
  if (UUT_->use_highbd_ == 0) {
    ref = ref8;
  } else {
    ref = CONVERT_TO_BYTEPTR(ref16);
  }
#else
  uint8_t ref[kOutputStride * kMaxDimension];
#endif

  // Populate ref and out with some random data
  ::libvpx_test::ACMRandom prng;
  for (int y = 0; y < Height(); ++y) {
    for (int x = 0; x < Width(); ++x) {
      uint16_t r;
#if CONFIG_VP9_HIGHBITDEPTH
      if (UUT_->use_highbd_ == 0 || UUT_->use_highbd_ == 8) {
        r = prng.Rand8Extremes();
      } else {
        r = prng.Rand16() & mask_;
      }
#else
      r = prng.Rand8Extremes();
#endif
      assign_val(out, y * kOutputStride + x, r);
      assign_val(ref, y * kOutputStride + x, r);
    }
  }

  for (int axis = 0; axis < 2; axis++) {
    int seed_val = 0;
    while (seed_val < 256) {
      for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
#if CONFIG_VP9_HIGHBITDEPTH
          assign_val(in, y * kOutputStride + x - SUBPEL_TAPS / 2 + 1,
                     ((seed_val >> (axis ? y : x)) & 1) * mask_);
#else
          assign_val(in, y * kOutputStride + x - SUBPEL_TAPS / 2 + 1,
                     ((seed_val >> (axis ? y : x)) & 1) * 255);
#endif
          if (axis) seed_val++;
        }
        if (axis) {
          seed_val -= 8;
        } else {
          seed_val++;
        }
      }
      if (axis) seed_val += 8;

      for (int filter_bank = 0; filter_bank < kNumFilterBanks; ++filter_bank) {
        const InterpKernel *filters =
            vp9_filter_kernels[static_cast<INTERP_FILTER>(filter_bank)];
        for (int filter_x = 0; filter_x < kNumFilters; ++filter_x) {
          for (int filter_y = 0; filter_y < kNumFilters; ++filter_y) {
            wrapper_filter_block2d_8_c(in, kInputStride, filters[filter_x],
                                       filters[filter_y], ref, kOutputStride,
                                       Width(), Height(), UUT_->use_highbd_);
            if (filter_x && filter_y)
              ASM_REGISTER_STATE_CHECK(UUT_->hv8_[0](
                  in, kInputStride, out, kOutputStride, filters[filter_x], 16,
                  filters[filter_y], 16, Width(), Height()));
            else if (filter_y)
              ASM_REGISTER_STATE_CHECK(UUT_->v8_[0](
                  in, kInputStride, out, kOutputStride, kInvalidFilter, 16,
                  filters[filter_y], 16, Width(), Height()));
            else if (filter_x)
              ASM_REGISTER_STATE_CHECK(UUT_->h8_[0](
                  in, kInputStride, out, kOutputStride, filters[filter_x], 16,
                  kInvalidFilter, 16, Width(), Height()));
            else
              ASM_REGISTER_STATE_CHECK(UUT_->copy_[0](
                  in, kInputStride, out, kOutputStride, kInvalidFilter, 0,
                  kInvalidFilter, 0, Width(), Height()));

            for (int y = 0; y < Height(); ++y) {
              for (int x = 0; x < Width(); ++x)
                ASSERT_EQ(lookup(ref, y * kOutputStride + x),
                          lookup(out, y * kOutputStride + x))
                    << "mismatch at (" << x << "," << y << "), "
                    << "filters (" << filter_bank << "," << filter_x << ","
                    << filter_y << ")";
            }
          }
        }
      }
    }
  }
}

/* This test exercises that enough rows and columns are filtered with every
   possible initial fractional positions and scaling steps. */
TEST_P(ConvolveTest, CheckScalingFiltering) {
  uint8_t *const in = input();
  uint8_t *const out = output();
  const InterpKernel *const eighttap = vp9_filter_kernels[EIGHTTAP];

  SetConstantInput(127);

  for (int frac = 0; frac < 16; ++frac) {
    for (int step = 1; step <= 32; ++step) {
      /* Test the horizontal and vertical filters in combination. */
      ASM_REGISTER_STATE_CHECK(
          UUT_->shv8_[0](in, kInputStride, out, kOutputStride, eighttap[frac],
                         step, eighttap[frac], step, Width(), Height()));

      CheckGuardBlocks();

      for (int y = 0; y < Height(); ++y) {
        for (int x = 0; x < Width(); ++x) {
          ASSERT_EQ(lookup(in, y * kInputStride + x),
                    lookup(out, y * kOutputStride + x))
              << "x == " << x << ", y == " << y << ", frac == " << frac
              << ", step == " << step;
        }
      }
    }
  }
}

using std::tr1::make_tuple;

#if CONFIG_VP9_HIGHBITDEPTH
#define WRAP(func, bd)                                                       \
  void wrap_##func##_##bd(                                                   \
      const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst,                \
      ptrdiff_t dst_stride, const int16_t *filter_x, int filter_x_stride,    \
      const int16_t *filter_y, int filter_y_stride, int w, int h) {          \
    vpx_highbd_##func(src, src_stride, dst, dst_stride, filter_x,            \
                      filter_x_stride, filter_y, filter_y_stride, w, h, bd); \
  }
#if HAVE_SSE2 && ARCH_X86_64
WRAP(convolve_copy_sse2, 8)
WRAP(convolve_avg_sse2, 8)
WRAP(convolve_copy_sse2, 10)
WRAP(convolve_avg_sse2, 10)
WRAP(convolve_copy_sse2, 12)
WRAP(convolve_avg_sse2, 12)
WRAP(convolve8_horiz_sse2, 8)
WRAP(convolve8_avg_horiz_sse2, 8)
WRAP(convolve8_vert_sse2, 8)
WRAP(convolve8_avg_vert_sse2, 8)
WRAP(convolve8_sse2, 8)
WRAP(convolve8_avg_sse2, 8)
WRAP(convolve8_horiz_sse2, 10)
WRAP(convolve8_avg_horiz_sse2, 10)
WRAP(convolve8_vert_sse2, 10)
WRAP(convolve8_avg_vert_sse2, 10)
WRAP(convolve8_sse2, 10)
WRAP(convolve8_avg_sse2, 10)
WRAP(convolve8_horiz_sse2, 12)
WRAP(convolve8_avg_horiz_sse2, 12)
WRAP(convolve8_vert_sse2, 12)
WRAP(convolve8_avg_vert_sse2, 12)
WRAP(convolve8_sse2, 12)
WRAP(convolve8_avg_sse2, 12)
#endif  // HAVE_SSE2 && ARCH_X86_64

WRAP(convolve_copy_c, 8)
WRAP(convolve_avg_c, 8)
WRAP(convolve8_horiz_c, 8)
WRAP(convolve8_avg_horiz_c, 8)
WRAP(convolve8_vert_c, 8)
WRAP(convolve8_avg_vert_c, 8)
WRAP(convolve8_c, 8)
WRAP(convolve8_avg_c, 8)
WRAP(convolve_copy_c, 10)
WRAP(convolve_avg_c, 10)
WRAP(convolve8_horiz_c, 10)
WRAP(convolve8_avg_horiz_c, 10)
WRAP(convolve8_vert_c, 10)
WRAP(convolve8_avg_vert_c, 10)
WRAP(convolve8_c, 10)
WRAP(convolve8_avg_c, 10)
WRAP(convolve_copy_c, 12)
WRAP(convolve_avg_c, 12)
WRAP(convolve8_horiz_c, 12)
WRAP(convolve8_avg_horiz_c, 12)
WRAP(convolve8_vert_c, 12)
WRAP(convolve8_avg_vert_c, 12)
WRAP(convolve8_c, 12)
WRAP(convolve8_avg_c, 12)
#undef WRAP

const ConvolveFunctions convolve8_c(
    wrap_convolve_copy_c_8, wrap_convolve_avg_c_8, wrap_convolve8_horiz_c_8,
    wrap_convolve8_avg_horiz_c_8, wrap_convolve8_vert_c_8,
    wrap_convolve8_avg_vert_c_8, wrap_convolve8_c_8, wrap_convolve8_avg_c_8,
    wrap_convolve8_horiz_c_8, wrap_convolve8_avg_horiz_c_8,
    wrap_convolve8_vert_c_8, wrap_convolve8_avg_vert_c_8, wrap_convolve8_c_8,
    wrap_convolve8_avg_c_8, 8);
const ConvolveFunctions convolve10_c(
    wrap_convolve_copy_c_10, wrap_convolve_avg_c_10, wrap_convolve8_horiz_c_10,
    wrap_convolve8_avg_horiz_c_10, wrap_convolve8_vert_c_10,
    wrap_convolve8_avg_vert_c_10, wrap_convolve8_c_10, wrap_convolve8_avg_c_10,
    wrap_convolve8_horiz_c_10, wrap_convolve8_avg_horiz_c_10,
    wrap_convolve8_vert_c_10, wrap_convolve8_avg_vert_c_10, wrap_convolve8_c_10,
    wrap_convolve8_avg_c_10, 10);
const ConvolveFunctions convolve12_c(
    wrap_convolve_copy_c_12, wrap_convolve_avg_c_12, wrap_convolve8_horiz_c_12,
    wrap_convolve8_avg_horiz_c_12, wrap_convolve8_vert_c_12,
    wrap_convolve8_avg_vert_c_12, wrap_convolve8_c_12, wrap_convolve8_avg_c_12,
    wrap_convolve8_horiz_c_12, wrap_convolve8_avg_horiz_c_12,
    wrap_convolve8_vert_c_12, wrap_convolve8_avg_vert_c_12, wrap_convolve8_c_12,
    wrap_convolve8_avg_c_12, 12);
const ConvolveParam kArrayConvolve_c[] = {
  ALL_SIZES(convolve8_c), ALL_SIZES(convolve10_c), ALL_SIZES(convolve12_c)
};

#else
const ConvolveFunctions convolve8_c(
    vpx_convolve_copy_c, vpx_convolve_avg_c, vpx_convolve8_horiz_c,
    vpx_convolve8_avg_horiz_c, vpx_convolve8_vert_c, vpx_convolve8_avg_vert_c,
    vpx_convolve8_c, vpx_convolve8_avg_c, vpx_scaled_horiz_c,
    vpx_scaled_avg_horiz_c, vpx_scaled_vert_c, vpx_scaled_avg_vert_c,
    vpx_scaled_2d_c, vpx_scaled_avg_2d_c, 0);
const ConvolveParam kArrayConvolve_c[] = { ALL_SIZES(convolve8_c) };
#endif
INSTANTIATE_TEST_CASE_P(C, ConvolveTest, ::testing::ValuesIn(kArrayConvolve_c));

#if HAVE_SSE2 && ARCH_X86_64
#if CONFIG_VP9_HIGHBITDEPTH
const ConvolveFunctions convolve8_sse2(
    wrap_convolve_copy_sse2_8, wrap_convolve_avg_sse2_8,
    wrap_convolve8_horiz_sse2_8, wrap_convolve8_avg_horiz_sse2_8,
    wrap_convolve8_vert_sse2_8, wrap_convolve8_avg_vert_sse2_8,
    wrap_convolve8_sse2_8, wrap_convolve8_avg_sse2_8,
    wrap_convolve8_horiz_sse2_8, wrap_convolve8_avg_horiz_sse2_8,
    wrap_convolve8_vert_sse2_8, wrap_convolve8_avg_vert_sse2_8,
    wrap_convolve8_sse2_8, wrap_convolve8_avg_sse2_8, 8);
const ConvolveFunctions convolve10_sse2(
    wrap_convolve_copy_sse2_10, wrap_convolve_avg_sse2_10,
    wrap_convolve8_horiz_sse2_10, wrap_convolve8_avg_horiz_sse2_10,
    wrap_convolve8_vert_sse2_10, wrap_convolve8_avg_vert_sse2_10,
    wrap_convolve8_sse2_10, wrap_convolve8_avg_sse2_10,
    wrap_convolve8_horiz_sse2_10, wrap_convolve8_avg_horiz_sse2_10,
    wrap_convolve8_vert_sse2_10, wrap_convolve8_avg_vert_sse2_10,
    wrap_convolve8_sse2_10, wrap_convolve8_avg_sse2_10, 10);
const ConvolveFunctions convolve12_sse2(
    wrap_convolve_copy_sse2_12, wrap_convolve_avg_sse2_12,
    wrap_convolve8_horiz_sse2_12, wrap_convolve8_avg_horiz_sse2_12,
    wrap_convolve8_vert_sse2_12, wrap_convolve8_avg_vert_sse2_12,
    wrap_convolve8_sse2_12, wrap_convolve8_avg_sse2_12,
    wrap_convolve8_horiz_sse2_12, wrap_convolve8_avg_horiz_sse2_12,
    wrap_convolve8_vert_sse2_12, wrap_convolve8_avg_vert_sse2_12,
    wrap_convolve8_sse2_12, wrap_convolve8_avg_sse2_12, 12);
const ConvolveParam kArrayConvolve_sse2[] = { ALL_SIZES(convolve8_sse2),
                                              ALL_SIZES(convolve10_sse2),
                                              ALL_SIZES(convolve12_sse2) };
#else
const ConvolveFunctions convolve8_sse2(
    vpx_convolve_copy_sse2, vpx_convolve_avg_sse2, vpx_convolve8_horiz_sse2,
    vpx_convolve8_avg_horiz_sse2, vpx_convolve8_vert_sse2,
    vpx_convolve8_avg_vert_sse2, vpx_convolve8_sse2, vpx_convolve8_avg_sse2,
    vpx_scaled_horiz_c, vpx_scaled_avg_horiz_c, vpx_scaled_vert_c,
    vpx_scaled_avg_vert_c, vpx_scaled_2d_c, vpx_scaled_avg_2d_c, 0);

const ConvolveParam kArrayConvolve_sse2[] = { ALL_SIZES(convolve8_sse2) };
#endif  // CONFIG_VP9_HIGHBITDEPTH
INSTANTIATE_TEST_CASE_P(SSE2, ConvolveTest,
                        ::testing::ValuesIn(kArrayConvolve_sse2));
#endif

#if HAVE_SSSE3
const ConvolveFunctions convolve8_ssse3(
    vpx_convolve_copy_c, vpx_convolve_avg_c, vpx_convolve8_horiz_ssse3,
    vpx_convolve8_avg_horiz_ssse3, vpx_convolve8_vert_ssse3,
    vpx_convolve8_avg_vert_ssse3, vpx_convolve8_ssse3, vpx_convolve8_avg_ssse3,
    vpx_scaled_horiz_c, vpx_scaled_avg_horiz_c, vpx_scaled_vert_c,
    vpx_scaled_avg_vert_c, vpx_scaled_2d_ssse3, vpx_scaled_avg_2d_c, 0);

const ConvolveParam kArrayConvolve8_ssse3[] = { ALL_SIZES(convolve8_ssse3) };
INSTANTIATE_TEST_CASE_P(SSSE3, ConvolveTest,
                        ::testing::ValuesIn(kArrayConvolve8_ssse3));
#endif

#if HAVE_AVX2 && HAVE_SSSE3
const ConvolveFunctions convolve8_avx2(
    vpx_convolve_copy_c, vpx_convolve_avg_c, vpx_convolve8_horiz_avx2,
    vpx_convolve8_avg_horiz_ssse3, vpx_convolve8_vert_avx2,
    vpx_convolve8_avg_vert_ssse3, vpx_convolve8_avx2, vpx_convolve8_avg_ssse3,
    vpx_scaled_horiz_c, vpx_scaled_avg_horiz_c, vpx_scaled_vert_c,
    vpx_scaled_avg_vert_c, vpx_scaled_2d_c, vpx_scaled_avg_2d_c, 0);

const ConvolveParam kArrayConvolve8_avx2[] = { ALL_SIZES(convolve8_avx2) };
INSTANTIATE_TEST_CASE_P(AVX2, ConvolveTest,
                        ::testing::ValuesIn(kArrayConvolve8_avx2));
#endif  // HAVE_AVX2 && HAVE_SSSE3

#if HAVE_NEON
const ConvolveFunctions convolve8_neon(
    vpx_convolve_copy_neon, vpx_convolve_avg_neon, vpx_convolve8_horiz_neon,
    vpx_convolve8_avg_horiz_neon, vpx_convolve8_vert_neon,
    vpx_convolve8_avg_vert_neon, vpx_convolve8_neon, vpx_convolve8_avg_neon,
    vpx_scaled_horiz_c, vpx_scaled_avg_horiz_c, vpx_scaled_vert_c,
    vpx_scaled_avg_vert_c, vpx_scaled_2d_c, vpx_scaled_avg_2d_c, 0);

const ConvolveParam kArrayConvolve8_neon[] = { ALL_SIZES(convolve8_neon) };
INSTANTIATE_TEST_CASE_P(NEON, ConvolveTest,
                        ::testing::ValuesIn(kArrayConvolve8_neon));
#endif  // HAVE_NEON

#if HAVE_DSPR2
const ConvolveFunctions convolve8_dspr2(
    vpx_convolve_copy_dspr2, vpx_convolve_avg_dspr2, vpx_convolve8_horiz_dspr2,
    vpx_convolve8_avg_horiz_dspr2, vpx_convolve8_vert_dspr2,
    vpx_convolve8_avg_vert_dspr2, vpx_convolve8_dspr2, vpx_convolve8_avg_dspr2,
    vpx_scaled_horiz_c, vpx_scaled_avg_horiz_c, vpx_scaled_vert_c,
    vpx_scaled_avg_vert_c, vpx_scaled_2d_c, vpx_scaled_avg_2d_c, 0);

const ConvolveParam kArrayConvolve8_dspr2[] = { ALL_SIZES(convolve8_dspr2) };
INSTANTIATE_TEST_CASE_P(DSPR2, ConvolveTest,
                        ::testing::ValuesIn(kArrayConvolve8_dspr2));
#endif  // HAVE_DSPR2

#if HAVE_MSA
const ConvolveFunctions convolve8_msa(
    vpx_convolve_copy_msa, vpx_convolve_avg_msa, vpx_convolve8_horiz_msa,
    vpx_convolve8_avg_horiz_msa, vpx_convolve8_vert_msa,
    vpx_convolve8_avg_vert_msa, vpx_convolve8_msa, vpx_convolve8_avg_msa,
    vpx_scaled_horiz_c, vpx_scaled_avg_horiz_c, vpx_scaled_vert_c,
    vpx_scaled_avg_vert_c, vpx_scaled_2d_c, vpx_scaled_avg_2d_c, 0);

const ConvolveParam kArrayConvolve8_msa[] = { ALL_SIZES(convolve8_msa) };
INSTANTIATE_TEST_CASE_P(MSA, ConvolveTest,
                        ::testing::ValuesIn(kArrayConvolve8_msa));
#endif  // HAVE_MSA
}  // namespace