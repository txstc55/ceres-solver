// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2015 Google Inc. All rights reserved.
// http://ceres-solver.org/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: sameeragarwal@google.com (Sameer Agarwal)
//         mierle@gmail.com (Keir Mierle)
//
// Finite differencing routine used by NumericDiffCostFunction.

#ifndef CERES_PUBLIC_INTERNAL_NUMERIC_DIFF_H_
#define CERES_PUBLIC_INTERNAL_NUMERIC_DIFF_H_

#include <cstring>

#include "Eigen/Dense"
#include "ceres/cost_function.h"
#include "ceres/internal/scoped_ptr.h"
#include "ceres/internal/variadic_evaluate.h"
#include "ceres/types.h"
#include "glog/logging.h"


namespace ceres {
namespace internal {

// Helper templates that allow evaluation of a variadic functor or a
// CostFunction object.
template <typename CostFunctor,
          int N0, int N1, int N2, int N3, int N4,
          int N5, int N6, int N7, int N8, int N9 >
bool EvaluateImpl(const CostFunctor* functor,
                  double const* const* parameters,
                  double* residuals,
                  const void* /* NOT USED */) {
  return VariadicEvaluate<CostFunctor,
                          double,
                          N0, N1, N2, N3, N4, N5, N6, N7, N8, N9>::Call(
                              *functor,
                              parameters,
                              residuals);
}

template <typename CostFunctor,
          int N0, int N1, int N2, int N3, int N4,
          int N5, int N6, int N7, int N8, int N9 >
bool EvaluateImpl(const CostFunctor* functor,
                  double const* const* parameters,
                  double* residuals,
                  const CostFunction* /* NOT USED */) {
  return functor->Evaluate(parameters, residuals, NULL);
}

// This is split from the main class because C++ doesn't allow partial template
// specializations for member functions. The alternative is to repeat the main
// class for differing numbers of parameters, which is also unfortunate.
template <typename CostFunctor,
          NumericDiffMethod kMethod,
          int kNumResiduals,
          int N0, int N1, int N2, int N3, int N4,
          int N5, int N6, int N7, int N8, int N9,
          int kParameterBlock,
          int kParameterBlockSize>
struct NumericDiff {
  // Mutates parameters but must restore them before return.
  static bool EvaluateJacobianForParameterBlock(
      const CostFunctor* functor,
      double const* residuals_at_eval_point,
      const double relative_step_size,
      int num_residuals,
      int parameter_block_index,
      int parameter_block_size,
      double **parameters,
      double *jacobian) {
    using Eigen::Map;
    using Eigen::Matrix;
    using Eigen::RowMajor;
    using Eigen::ColMajor;

    const int num_residuals_internal =
        (kNumResiduals != ceres::DYNAMIC ? kNumResiduals : num_residuals);
    const int parameter_block_index_internal =
        (kParameterBlock != ceres::DYNAMIC ? kParameterBlock :
                                             parameter_block_index);
    const int parameter_block_size_internal =
        (kParameterBlockSize != ceres::DYNAMIC ? kParameterBlockSize :
                                                 parameter_block_size);

    typedef Matrix<double, kNumResiduals, 1> ResidualVector;
    typedef Matrix<double, kParameterBlockSize, 1> ParameterVector;

    // The convoluted reasoning for choosing the Row/Column major
    // ordering of the matrix is an artifact of the restrictions in
    // Eigen that prevent it from creating RowMajor matrices with a
    // single column. In these cases, we ask for a ColMajor matrix.
    typedef Matrix<double,
                   kNumResiduals,
                   kParameterBlockSize,
                   (kParameterBlockSize == 1) ? ColMajor : RowMajor>
        JacobianMatrix;

    Map<JacobianMatrix> parameter_jacobian(jacobian,
                                           num_residuals_internal,
                                           parameter_block_size_internal);

    // Mutate 1 element at a time and then restore.
    Map<ParameterVector> x_plus_delta(
        parameters[parameter_block_index_internal],
        parameter_block_size_internal);
    ParameterVector x(x_plus_delta);
    ParameterVector step_size = x.array().abs() * relative_step_size;

    // It is not a good idea to make the step size arbitrarily
    // small. This will lead to problems with round off and numerical
    // instability when dividing by the step size. The general
    // recommendation is to not go down below sqrt(epsilon).
    const double min_step_size =
        std::sqrt(std::numeric_limits<double>::epsilon());

    // For each parameter in the parameter block, use finite differences to
    // compute the derivative for that parameter.
    ResidualVector residuals(num_residuals_internal);
    for (int j = 0; j < parameter_block_size_internal; ++j) {
      const double delta = std::max(min_step_size, step_size(j));
      x_plus_delta(j) = x(j) + delta;

      if (!EvaluateImpl<CostFunctor, N0, N1, N2, N3, N4, N5, N6, N7, N8, N9>(
              functor, parameters, residuals.data(), functor)) {
        return false;
      }

      // Compute this column of the jacobian in 3 steps:
      // 1. Store residuals for the forward part.
      // 2. Subtract residuals for the backward (or 0) part.
      // 3. Divide out the run.
      parameter_jacobian.col(j) = residuals;

      double one_over_delta = 1.0 / delta;
      if (kMethod == CENTRAL) {
        // Compute the function on the other side of x(j).
        x_plus_delta(j) = x(j) - delta;

        if (!EvaluateImpl<CostFunctor, N0, N1, N2, N3, N4, N5, N6, N7, N8, N9>(
                functor, parameters, residuals.data(), functor)) {
          return false;
        }

        parameter_jacobian.col(j) -= residuals;
        one_over_delta /= 2;
      } else {
        // Forward difference only; reuse existing residuals evaluation.
        parameter_jacobian.col(j) -=
            Map<const ResidualVector>(residuals_at_eval_point,
                                      num_residuals_internal);
      }
      x_plus_delta(j) = x(j);  // Restore x_plus_delta.

      // Divide out the run to get slope.
      parameter_jacobian.col(j) *= one_over_delta;
    }
    return true;
  }
};

template <typename CostFunctor,
          NumericDiffMethod kMethod,
          int kNumResiduals,
          int N0, int N1, int N2, int N3, int N4,
          int N5, int N6, int N7, int N8, int N9,
          int kParameterBlock>
struct NumericDiff<CostFunctor, kMethod, kNumResiduals,
                   N0, N1, N2, N3, N4, N5, N6, N7, N8, N9,
                   kParameterBlock, 0> {
  // Mutates parameters but must restore them before return.
  static bool EvaluateJacobianForParameterBlock(
      const CostFunctor* functor,
      double const* residuals_at_eval_point,
      const double relative_step_size,
      const int num_residuals,
      const int parameter_block_index,
      const int parameter_block_size,
      double **parameters,
      double *jacobian) {
    // Silence unused parameter compiler warnings.
    (void)functor;
    (void)residuals_at_eval_point;
    (void)relative_step_size;
    (void)num_residuals;
    (void)parameter_block_index;
    (void)parameter_block_size;
    (void)parameters;
    (void)jacobian;
    LOG(FATAL) << "Control should never reach here.";
    return true;
  }
};

}  // namespace internal
}  // namespace ceres

#endif  // CERES_PUBLIC_INTERNAL_NUMERIC_DIFF_H_
