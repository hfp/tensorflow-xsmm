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

#ifndef TENSORFLOW_CONTRIB_LITE_TOOLS_ACCURACY_IMAGENET_MODEL_EVALUATOR_H_
#define TENSORFLOW_CONTRIB_LITE_TOOLS_ACCURACY_IMAGENET_MODEL_EVALUATOR_H_
#include <string>
#include <vector>

#include "tensorflow/contrib/lite/tools/accuracy/imagenet_topk_eval.h"
#include "tensorflow/contrib/lite/tools/accuracy/utils.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "tensorflow/core/lib/core/status.h"

namespace tensorflow {
namespace metrics {

// Evaluates models accuracy for ILSVRC dataset.
//
// Generates the top-1, top-k accuracy counts where k is
// controlled by |num_ranks|.
// Usage:
// ModelInfo model_info = ..
// ImagenetModelEvaluator::Params params;
// .. set params to image, label, output label and model file path..
// SomeObserver observer;
// ImagenetModelEvaluator evaluator(model_info, params);
// evaluator.AddObserver(&observer);
// TF_CHECK_OK(evaluator.EvaluateModel());
class ImagenetModelEvaluator {
 public:
  struct Params {
    // Path to ground truth images.
    string ground_truth_images_path;

    // Path to labels file for ground truth image.
    // This file should be generated with the scripts.
    string ground_truth_labels_path;

    // This is word labels generated by the model. The category
    // indices of output probabilities generated by the model maybe different
    // from the indices in the imagenet dataset.
    string model_output_labels_path;

    // Path to the model file.
    string model_file_path;

    // The maximum number of images to calculate accuracy.
    // 0 means all images, a positive number means only the specified
    // number of images.
    int number_of_images = 0;

    // Number of ranks, top K.
    int num_ranks = 10;
  };

  // An evaluation observer.
  class Observer {
   public:
    Observer() = default;
    Observer(const Observer&) = delete;
    Observer& operator=(const Observer&) = delete;

    Observer(const Observer&&) = delete;
    Observer& operator=(const Observer&&) = delete;

    // Called on start of evaluation.
    virtual void OnEvaluationStart(int total_number_of_images) = 0;

    // Called when evaluation was complete for `image`.
    virtual void OnSingleImageEvaluationComplete(
        const ImagenetTopKAccuracy::AccuracyStats& stats,
        const string& image) = 0;

    virtual ~Observer() = default;
  };

  ImagenetModelEvaluator(const utils::ModelInfo& model_info,
                         const Params& params)
      : model_info_(model_info), params_(params) {}

  // Factory method to create the evaluator by parsing command line arguments.
  static Status Create(int argc, char* argv[],
                       std::unique_ptr<ImagenetModelEvaluator>* evaluator);

  // Adds an observer that can observe evaluation events..
  void AddObserver(Observer* observer) { observers_.push_back(observer); }

  const Params& params() { return params_; }

  // Evaluates the provided model over the dataset.
  Status EvaluateModel();

 private:
  std::vector<Observer*> observers_;
  const utils::ModelInfo model_info_;
  const Params params_;
};

}  // namespace metrics
}  // namespace tensorflow
#endif  // TENSORFLOW_CONTRIB_LITE_TOOLS_ACCURACY_IMAGENET_MODEL_EVALUATOR_H_
