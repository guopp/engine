// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_FLOW_DISPLAY_LIST_COMPLEXITY_HELPER_H_
#define FLUTTER_FLOW_DISPLAY_LIST_COMPLEXITY_HELPER_H_

#include "flutter/display_list/display_list_blend_mode.h"
#include "flutter/display_list/display_list_complexity.h"
#include "flutter/display_list/display_list_dispatcher.h"
#include "flutter/display_list/display_list_utils.h"

namespace flutter {

// The Metal and OpenGL complexity calculators use benchmark data gathered
// using the display_list_benchmarks test suite on real devices.
//
// Constants of proportionality and trends were chosen to better match
// larger numbers rather than smaller ones. This may turn out to be the
// wrong decision, but the rationale here is that with the smaller numbers,
// we have:
//
// a) More noise.
// b) Less absolute difference. If the constant we've chosen is out by 50%
//    on a measurement that is 0.001ms, that's less of an issue than if
//    the measurement is 15ms.
// c) Smaller numbers affect the caching decision negligibly; the caching
//    decision is likely to be driven by slower ops rather than faster ones.
//
// In some cases, a cost penalty is used to figure out the cost of an
// attribute such as anti-aliasing or fill style. In some of these, the
// penalty is proportional to some other value such as the radius of
// a circle. In these cases, we ensure that the penalty is never smaller
// than 1.0f.
//
// The error bars in measurement are likely quite large and will
// vary from device to device, so the goal here is not to provide a
// perfectly accurate representation of a given DisplayList's
// complexity but rather a very rough estimate that improves upon our
// previous cache admission policy (op_count > 5).
//
// There will likely be future work that will improve upon the figures
// in here. Notably, we do not take matrices or clips into account yet.
//
// The scoring is based around a baseline score of 100 being roughly
// equivalent to 0.0005ms of time. With a 32-bit unsigned integer, this
// would set the maximum time estimate associated with the complexity score
// at about 21 seconds, which far exceeds what we would ever expect a
// DisplayList to take rasterising.
//
// Finally, care has been taken to keep the computations as cheap as possible.
// We need to be able to calculate the complexity as quickly as possible
// so that we don't end up wasting too much time figuring out if something
// should be cached or not and eat into the time we could have just spent
// rasterising the DisplayList.
//
// In order to keep the computations cheap, the following tradeoffs were made:
//
// a) Limit all computations to simple division, multiplication, addition
//    and subtraction.
// b) Try and use integer arithmetic as much as possible.
// c) If a specific draw op is logarithmic in complexity, do a best fit
//    onto a linear equation within the range we expect to see the variables
//    fall within.
//
// As an example of the above, let's say we have some data that looks like the
// complexity is something like O(n^1/3). We would like to avoid anything too
// expensive to calculate, so taking the cube root of the value to try and
// calculate the time cost should be avoided.
//
// In this case, the approach would be to take a straight line approximation
// that maps closely in the range of n where we feel it is most likely to occur.
// For example, if this were drawLine, and n were the line length, and we
// expected the line lengths to typically be between 50 and 100, then we would
// figure out the equation of the straight line graph that approximates the
// n^1/3 curve, and if possible try and choose an approximation that is more
// representative in the range of [50, 100] for n.
//
// Once that's done, we can develop the formula as follows:
//
// Take y = mx + c (straight line graph chosen as per guidelines above).
// Divide by however many ops the benchmark ran for a single pass.
// Multiply by 200,000 to normalise 0.0005ms = 100.
// Simplify down the formula.
//
// So if we had m = 1/5 and c = 0, and the drawLines benchmark ran 10,000
// drawLine calls per iteration:
//
//   y (time taken) = x (line length) / 5
//   y = x / 5 * 200,000 / 10,000
//   y = x / 5 * 20
//   y = 4x

class ComplexityCalculatorHelper
    : public virtual Dispatcher,
      public virtual IgnoreClipDispatchHelper,
      public virtual IgnoreTransformDispatchHelper {
 public:
  ComplexityCalculatorHelper(unsigned int ceiling)
      : is_complex_(false), ceiling_(ceiling), complexity_score_(0) {}

  virtual ~ComplexityCalculatorHelper() = default;

  void setDither(bool dither) override {}
  void setInvertColors(bool invert) override {}
  void setStrokeCap(SkPaint::Cap cap) override {}
  void setStrokeJoin(SkPaint::Join join) override {}
  void setStrokeMiter(SkScalar limit) override {}
  void setColor(SkColor color) override {}
  void setBlendMode(DlBlendMode mode) override {}
  void setBlender(sk_sp<SkBlender> blender) override {}
  void setColorSource(const DlColorSource* source) override {}
  void setImageFilter(sk_sp<SkImageFilter> filter) override {}
  void setColorFilter(const DlColorFilter* filter) override {}
  void setPathEffect(sk_sp<SkPathEffect> effect) override {}
  void setMaskFilter(const DlMaskFilter* filter) override {}

  void save() override {}
  // We accumulate the cost of restoring a saveLayer() in saveLayer()
  void restore() override {}

  void setAntiAlias(bool aa) override { current_paint_.setAntiAlias(aa); }

  void setStyle(SkPaint::Style style) override {
    current_paint_.setStyle(style);
  }

  void setStrokeWidth(SkScalar width) override {
    current_paint_.setStrokeWidth(width);
  }

  void drawColor(SkColor color, DlBlendMode mode) override {
    if (IsComplex()) {
      return;
    }
    // Placeholder value here. This is a relatively cheap operation.
    AccumulateComplexity(50);
  }

  void drawPaint() override {
    if (IsComplex()) {
      return;
    }
    // Placeholder value here. This can be cheap (e.g. effectively a drawColor),
    // or expensive (e.g. a bitmap shader with an image filter)
    AccumulateComplexity(50);
  }

  void drawImageRect(const sk_sp<SkImage> image,
                     const SkRect& src,
                     const SkRect& dst,
                     const SkSamplingOptions& sampling,
                     bool render_with_attributes,
                     SkCanvas::SrcRectConstraint constraint) override {
    if (IsComplex()) {
      return;
    }
    ImageRect(image->dimensions(), image->isTextureBacked(),
              render_with_attributes, constraint);
  }

  void drawImageLattice(const sk_sp<SkImage> image,
                        const SkCanvas::Lattice& lattice,
                        const SkRect& dst,
                        SkFilterMode filter,
                        bool render_with_attributes) override {
    if (IsComplex()) {
      return;
    }
    // This is not currently called from Flutter code, and this API is likely
    // to be removed in the future. For now, just return what drawImageNine
    // would
    ImageRect(image->dimensions(), image->isTextureBacked(),
              render_with_attributes,
              SkCanvas::SrcRectConstraint::kStrict_SrcRectConstraint);
  }

  void drawAtlas(const sk_sp<SkImage> atlas,
                 const SkRSXform xform[],
                 const SkRect tex[],
                 const SkColor colors[],
                 int count,
                 DlBlendMode mode,
                 const SkSamplingOptions& sampling,
                 const SkRect* cull_rect,
                 bool render_with_attributes) override {
    if (IsComplex()) {
      return;
    }
    // This API just does a series of drawImage calls from the atlas
    // This is equivalent to calling drawImageRect lots of times
    for (int i = 0; i < count; i++) {
      ImageRect(SkISize::Make(tex[i].width(), tex[i].height()), true,
                render_with_attributes,
                SkCanvas::SrcRectConstraint::kStrict_SrcRectConstraint);
    }
  }

  void drawPicture(const sk_sp<SkPicture> picture,
                   const SkMatrix* matrix,
                   bool render_with_attributes) override {
    // This API shouldn't be used, but for now just take the
    // approximateOpCount() and multiply by 50 as a placeholder.
    AccumulateComplexity(picture->approximateOpCount() * 50);
  }

  // This method finalizes the complexity score calculation and returns it
  unsigned int ComplexityScore() {
    // We hit our ceiling, so return that
    if (IsComplex()) {
      return Ceiling();
    }

    // Calculate the impact of any draw ops where the complexity is dependent
    // on the number of calls made.
    unsigned int batched_complexity = BatchedComplexity();

    // Check for overflow
    if (Ceiling() - complexity_score_ < batched_complexity) {
      return Ceiling();
    }

    return complexity_score_ + batched_complexity;
  }

 protected:
  void AccumulateComplexity(unsigned int complexity) {
    // Check to see if we will overflow by accumulating this complexity score
    if (ceiling_ - complexity_score_ < complexity) {
      is_complex_ = true;
      return;
    }

    complexity_score_ += complexity;
  }

  inline bool IsAntiAliased() { return current_paint_.isAntiAlias(); }
  inline bool IsHairline() { return current_paint_.getStrokeWidth() == 0.0f; }
  inline SkPaint::Style Style() { return current_paint_.getStyle(); }
  inline bool IsComplex() { return is_complex_; }
  inline unsigned int Ceiling() { return ceiling_; }
  inline unsigned int CurrentComplexityScore() { return complexity_score_; }

  unsigned int CalculatePathComplexity(const SkPath& path,
                                       unsigned int line_verb_cost,
                                       unsigned int quad_verb_cost,
                                       unsigned int conic_verb_cost,
                                       unsigned int cubic_verb_cost) {
    int verb_count = path.countVerbs();
    uint8_t verbs[verb_count];
    path.getVerbs(verbs, verb_count);

    unsigned int complexity = 0;
    for (int i = 0; i < verb_count; i++) {
      switch (verbs[i]) {
        case SkPath::Verb::kLine_Verb:
          complexity += line_verb_cost;
          break;
        case SkPath::Verb::kQuad_Verb:
          complexity += quad_verb_cost;
          break;
        case SkPath::Verb::kConic_Verb:
          complexity += conic_verb_cost;
          break;
        case SkPath::Verb::kCubic_Verb:
          complexity += cubic_verb_cost;
          break;
      }
    }
    return complexity;
  }

  virtual void ImageRect(const SkISize& size,
                         bool texture_backed,
                         bool render_with_attributes,
                         SkCanvas::SrcRectConstraint constraint) = 0;

  // This calculates and returns the cost of draw calls which are batched and
  // thus have a time cost proportional to the number of draw calls made, such
  // as saveLayer and drawTextBlob.
  virtual unsigned int BatchedComplexity() = 0;

 private:
  SkPaint current_paint_;

  // If we exceed the ceiling (defaults to the largest number representable
  // by unsigned int), then set the is_complex_ bool and we no longer
  // accumulate.
  bool is_complex_;
  unsigned int ceiling_;

  unsigned int complexity_score_;
};

}  // namespace flutter

#endif  // FLUTTER_FLOW_DISPLAY_LIST_COMPLEXITY_HELPER_H_
