# Copyright 2018 The TensorFlow Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================
"""Ordered bijector."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function


from tensorflow.python.framework import tensor_shape
from tensorflow.python.ops import array_ops
from tensorflow.python.ops import check_ops
from tensorflow.python.ops import control_flow_ops
from tensorflow.python.ops import math_ops
from tensorflow.python.ops.distributions import bijector
from tensorflow.python.util import deprecation


__all__ = [
    "Ordered",
]


class Ordered(bijector.Bijector):
  """Bijector which maps a tensor x_k that has increasing elements in the last
  dimension to an unconstrained tensor y_k.

  Both the domain and the codomain of the mapping is [-inf, inf], however,
  the input of the forward mapping must be strictly increasing.
  The inverse of the bijector applied to a normal random vector `y ~ N(0, 1)`
  gives back a sorted random vector with the same distribution `x ~ N(0, 1)`
  where `x = sort(y)`

  On the last dimension of the tensor, Ordered bijector performs:
  `y[0] = x[0]`
  `y[1:] = math_ops.log(x[1:] - x[:-1])`

  #### Example Use:

  ```python
  bijector.Ordered().forward([2, 3, 4])
  # Result: [2., 0., 0.]

  bijector.Ordered().inverse([0.06428002, -1.07774478, -0.71530371])
  # Result: [0.06428002, 0.40464228, 0.8936858]
  ```
  """

  @deprecation.deprecated(
      "2018-10-01",
      "The TensorFlow Distributions library has moved to "
      "TensorFlow Probability "
      "(https://github.com/tensorflow/probability). You "
      "should update all references to use `tfp.distributions` "
      "instead of `tfp.distributions`.",
      warn_once=True)
  def __init__(self, validate_args=False, name="ordered"):
    super(Ordered, self).__init__(
        forward_min_event_ndims=1,
        validate_args=validate_args,
        name=name)

  def _forward_event_shape(self, input_shape):
    if input_shape.ndims is None or input_shape[-1] is None:
      return input_shape
    return tensor_shape.TensorShape([input_shape[-1]])

  def _forward_event_shape_tensor(self, input_shape):
    return (input_shape[-1])[..., array_ops.newaxis]

  def _inverse_event_shape(self, output_shape):
    if output_shape.ndims is None or output_shape[-1] is None:
      return output_shape
    if output_shape[-1] <= 1:
      raise ValueError("output_shape[-1] = %d <= 1" % output_shape[-1])
    return tensor_shape.TensorShape([output_shape[-1]])

  def _inverse_event_shape_tensor(self, output_shape):
    if self.validate_args:
      is_greater_one = check_ops.assert_greater(
          output_shape[-1], 1, message="Need last dimension greater than 1.")
      output_shape = control_flow_ops.with_dependencies(
          [is_greater_one], output_shape)
    return (output_shape[-1])[..., array_ops.newaxis]

  def _forward(self, x):
    x = self._maybe_assert_valid_x(x)
    y0 = x[..., 0, array_ops.newaxis]
    yk = math_ops.log(x[..., 1:] - x[..., :-1])
    y = array_ops.concat([y0, yk], axis=-1)
    return y

  def _inverse(self, y):
    x0 = y[..., 0, array_ops.newaxis]
    xk = math_ops.exp(y[..., 1:])
    x = array_ops.concat([x0, xk], axis=-1)
    return math_ops.cumsum(x, axis=-1)

  def _inverse_log_det_jacobian(self, y):
    # The Jacobian of the inverse mapping is lower
    # triangular, with the diagonal elements being:
    # J[i,i] = 1 if i=1, and
    #          exp(y_i) if 1<i<=K
    # which gives the absolute Jacobian determinant:
    # |det(Jac)| = prod_{i=1}^{K} exp(y[i]).
    # (1) - Stan Modeling Language User's Guide and Reference Manual
    #       Version 2.17.0 session 35.2
    return math_ops.reduce_sum(y[..., 1:], axis=-1)

  def _forward_log_det_jacobian(self, x):
    x = self._maybe_assert_valid_x(x)
    return -math_ops.reduce_sum(
        math_ops.log(x[..., 1:] - x[..., :-1]),
        axis=-1)

  def _maybe_assert_valid_x(self, x):
    if not self.validate_args:
      return x
    is_valid = check_ops.assert_positive(
        x[..., 1:] - x[..., :-1],
        message="Forward transformation input must be strictly increasing.")
    return control_flow_ops.with_dependencies([is_valid], x)
