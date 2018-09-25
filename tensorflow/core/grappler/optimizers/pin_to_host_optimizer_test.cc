/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/core/grappler/optimizers/pin_to_host_optimizer.h"
#include "tensorflow/cc/ops/standard_ops.h"
#include "tensorflow/core/framework/node_def.pb.h"
#include "tensorflow/core/framework/tensor_testutil.h"
#include "tensorflow/core/grappler/grappler_item.h"
#include "tensorflow/core/grappler/utils/grappler_test.h"
#include "tensorflow/core/lib/core/status_test_util.h"
#include "tensorflow/core/platform/test.h"

namespace tensorflow {
namespace grappler {
namespace {

class PinToHostOptimizerTest : public GrapplerTest {};

TEST_F(PinToHostOptimizerTest, TryFindHostDevice) {
  gtl::FlatSet<string> devices = {};
  EXPECT_EQ("ABC", internal::TryFindHostDevice(devices, false, "ABC"));

  devices = {"/device:CPU:0", "/device:XLA_GPU:0"};
  EXPECT_EQ(internal::TryFindHostDevice(devices, true, ""), "/device:CPU:0");
  EXPECT_EQ(internal::TryFindHostDevice(devices, true, "/device:XLA_GPU:0"),
            "/device:CPU:0");
  EXPECT_EQ(internal::TryFindHostDevice(devices, true, "/device:XLA_GPU:*"),
            "/device:CPU:0");

  devices = {"/device:XLA_CPU:0", "/device:XLA_GPU:0"};
  EXPECT_EQ(internal::TryFindHostDevice(devices, false, ""), "");
  EXPECT_EQ(internal::TryFindHostDevice(devices, false, "/device:XLA_GPU:0"),
            "/device:XLA_CPU:0");
  EXPECT_EQ(internal::TryFindHostDevice(devices, false, "/device:XLA_GPU:*"),
            "/device:XLA_CPU:0");

  devices = {"/device:XLA_GPU:0"};
  EXPECT_EQ(internal::TryFindHostDevice(devices, false, ""), "");
  EXPECT_EQ(internal::TryFindHostDevice(devices, false, "/device:XLA_GPU:0"),
            "/device:XLA_GPU:0");
  EXPECT_EQ(internal::TryFindHostDevice(devices, false, "/device:XLA_GPU:*"),
            "/device:XLA_GPU:*");
}

TEST_F(PinToHostOptimizerTest, OptimizeSmallOpsToHost) {
  tensorflow::Scope s = tensorflow::Scope::NewRootScope();
  Output a = ops::Const(s.WithOpName("a"), 1, {1024, 1024});
  Output c = ops::Shape(s.WithOpName("c"), a);
  Output d = ops::Const(s.WithOpName("d"), 0, {1});
  Output e = ops::ReduceProd(s.WithOpName("e"), c, d);

  GrapplerItem item;
  item.fetch = {"a", "c", "d", "e"};
  TF_CHECK_OK(s.ToGraphDef(&item.graph));

  auto tensors_expected = EvaluateNodes(item.graph, item.fetch);

  GraphDef output;
  PinToHostOptimizer optimizer(RewriterConfig::ON);
  TF_EXPECT_OK(optimizer.Optimize(nullptr, item, &output));

  auto tensors = EvaluateNodes(item.graph, item.fetch);
  EXPECT_EQ(tensors_expected.size(), tensors.size());
  for (int i = 0; i < tensors.size(); ++i) {
    test::ExpectTensorEqual<int32>(tensors[i], tensors_expected[i]);
  }

  int found = 0;
  for (const NodeDef& node : output.node()) {
    if (node.name() == "a" || node.name() == "c") {
      EXPECT_TRUE(node.device().empty());
    } else if (node.name() == "d" || node.name() == "e") {
      EXPECT_EQ(node.device(), "/device:CPU:0");
    }
    ++found;
  }
  EXPECT_EQ(found, 4);
}

TEST_F(PinToHostOptimizerTest, TopologicalSort) {
  tensorflow::Scope s = tensorflow::Scope::NewRootScope();
  Output a = ops::Const(s.WithOpName("a"), 1, {1024, 1024});
  Output c = ops::Shape(s.WithOpName("c"), a);
  Output d = ops::Const(s.WithOpName("d"), 0, {1});
  Output e = ops::ReduceProd(s.WithOpName("e"), c, d);

  GrapplerItem item;
  item.fetch = {"a", "c", "d", "e"};
  TF_CHECK_OK(s.ToGraphDef(&item.graph));

  auto tensors_expected = EvaluateNodes(item.graph, item.fetch);

  // Reverse the graph, and hence rely on the optimizer to sort it.
  std::reverse(item.graph.mutable_node()->begin(),
               item.graph.mutable_node()->end());

  GraphDef output;
  PinToHostOptimizer optimizer(RewriterConfig::ON);
  TF_EXPECT_OK(optimizer.Optimize(nullptr, item, &output));

  auto tensors = EvaluateNodes(item.graph, item.fetch);
  EXPECT_EQ(tensors_expected.size(), tensors.size());
  for (int i = 0; i < tensors.size(); ++i) {
    test::ExpectTensorEqual<int32>(tensors[i], tensors_expected[i]);
  }

  int found = 0;
  for (const NodeDef& node : output.node()) {
    if (node.name() == "a" || node.name() == "c") {
      EXPECT_TRUE(node.device().empty());
    } else if (node.name() == "d" || node.name() == "e") {
      EXPECT_EQ(node.device(), "/device:CPU:0");
    }
    ++found;
  }
  EXPECT_EQ(found, 4);
}

TEST_F(PinToHostOptimizerTest, NoSwap) {
  tensorflow::Scope s = tensorflow::Scope::NewRootScope();
  // `b` should be too big to swap, consequently `c` should not be swapped.
  // PinToHostOptimizer should then detect that `a` should not be swapped.
  Output a = ops::Const(s.WithOpName("a"), 1, {1, 1});
  Output b = ops::Const(s.WithOpName("b"), 1, {1, 1024 * 1024});
  Output c = ops::MatMul(s.WithOpName("c"), a, b);

  GrapplerItem item;
  item.fetch = {"a", "b", "c"};
  TF_CHECK_OK(s.ToGraphDef(&item.graph));

  auto tensors_expected = EvaluateNodes(item.graph, item.fetch);

  GraphDef output;
  PinToHostOptimizer optimizer(RewriterConfig::ON);
  TF_EXPECT_OK(optimizer.Optimize(nullptr, item, &output));

  auto tensors = EvaluateNodes(item.graph, item.fetch);
  EXPECT_EQ(tensors_expected.size(), tensors.size());
  for (int i = 0; i < tensors.size(); ++i) {
    test::ExpectTensorEqual<int32>(tensors[i], tensors_expected[i]);
  }

  int found = 0;
  for (const NodeDef& node : output.node()) {
    EXPECT_TRUE(node.device().empty());
    ++found;
  }
  EXPECT_EQ(found, 3);
}

TEST_F(PinToHostOptimizerTest, PortIdToArgId) {
  tensorflow::Scope s = tensorflow::Scope::NewRootScope();
  Output a = ops::Const(s.WithOpName("a"), 1, {1, 2, 3});
  ops::ShapeN b(s.WithOpName("b"), {a, a, a});

  GrapplerItem item;
  item.fetch = {"a", "b"};
  TF_CHECK_OK(s.ToGraphDef(&item.graph));

  auto tensors_expected = EvaluateNodes(item.graph, item.fetch);

  GraphDef output;
  PinToHostOptimizer optimizer(RewriterConfig::ON);
  TF_EXPECT_OK(optimizer.Optimize(nullptr, item, &output));

  auto tensors = EvaluateNodes(item.graph, item.fetch);
  EXPECT_EQ(tensors_expected.size(), tensors.size());
  for (int i = 0; i < tensors.size(); ++i) {
    test::ExpectTensorEqual<int32>(tensors[i], tensors_expected[i]);
  }

  int found = 0;
  for (const NodeDef& node : output.node()) {
    EXPECT_EQ(node.device(), "/device:CPU:0");
    ++found;
  }
  EXPECT_EQ(found, 2);
}

}  // namespace
}  // namespace grappler
}  // namespace tensorflow
