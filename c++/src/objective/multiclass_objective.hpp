#ifndef LIGHTGBM_OBJECTIVE_MULTICLASS_OBJECTIVE_HPP_
#define LIGHTGBM_OBJECTIVE_MULTICLASS_OBJECTIVE_HPP_

#include <LightGBM/objective_function.h>

#include <cstring>
#include <cmath>
#include <vector>

#include "binary_objective.hpp"

namespace LightGBM {
/*!
* \brief Objective function for multiclass classification, use softmax as objective functions
*/
class MulticlassSoftmax: public ObjectiveFunction {
public:
  explicit MulticlassSoftmax(const ObjectiveConfig& config) {
    num_class_ = config.num_class;
  }

  explicit MulticlassSoftmax(const std::vector<std::string>& strs) {
    num_class_ = -1;
    for (auto str : strs) {
      auto tokens = Common::Split(str.c_str(), ':');
      if (tokens.size() == 2) {
        if (tokens[0] == std::string("num_class")) {
          Common::Atoi(tokens[1].c_str(), &num_class_);
        }
      }
    }
    if (num_class_ < 0) {
      Log::Fatal("Objective should contains num_class field");
    }
  }

  ~MulticlassSoftmax() {

  }

  void Init(const Metadata& metadata, data_size_t num_data) override {
    num_data_ = num_data;
    label_ = metadata.label();
    weights_ = metadata.weights();
    label_int_.resize(num_data_);
    for (int i = 0; i < num_data_; ++i) {
      label_int_[i] = static_cast<int>(label_[i]);
      if (label_int_[i] < 0 || label_int_[i] >= num_class_) {
        Log::Fatal("Label must be in [0, %d), but found %d in label", num_class_, label_int_[i]);
      }
    }
  }

  void GetGradients(const double* score, score_t* gradients, score_t* hessians) const override {
    if (weights_ == nullptr) {
      std::vector<double> rec;
      #pragma omp parallel for schedule(static) private(rec)
      for (data_size_t i = 0; i < num_data_; ++i) {
        rec.resize(num_class_);
        for (int k = 0; k < num_class_; ++k) {
          size_t idx = static_cast<size_t>(num_data_) * k + i;
          rec[k] = static_cast<double>(score[idx]);
        }
        Common::Softmax(&rec);
        for (int k = 0; k < num_class_; ++k) {
          auto p = rec[k];
          size_t idx = static_cast<size_t>(num_data_) * k + i;
          if (label_int_[i] == k) {
            gradients[idx] = static_cast<score_t>(p - 1.0f);
          } else {
            gradients[idx] = static_cast<score_t>(p);
          }
          hessians[idx] = static_cast<score_t>(2.0f * p * (1.0f - p));
        }
      }
    } else {
      std::vector<double> rec;
      #pragma omp parallel for schedule(static) private(rec)
      for (data_size_t i = 0; i < num_data_; ++i) {
        rec.resize(num_class_);
        for (int k = 0; k < num_class_; ++k) {
          size_t idx = static_cast<size_t>(num_data_) * k + i;
          rec[k] = static_cast<double>(score[idx]);
        }
        Common::Softmax(&rec);
        for (int k = 0; k < num_class_; ++k) {
          auto p = rec[k];
          size_t idx = static_cast<size_t>(num_data_) * k + i;
          if (label_int_[i] == k) {
            gradients[idx] = static_cast<score_t>((p - 1.0f) * weights_[i]);
          } else {
            gradients[idx] = static_cast<score_t>((p) * weights_[i]);
          }
          hessians[idx] = static_cast<score_t>((2.0f * p * (1.0f - p))* weights_[i]);
        }
      }
    }
  }

  void ConvertOutput(const double* input, double* output) const override {
    Common::Softmax(input, output, num_class_);
  }

  const char* GetName() const override {
    return "multiclass";
  }

  std::string ToString() const override {
    std::stringstream str_buf;
    str_buf << GetName() << " ";
    str_buf << "num_class:" << num_class_;
    return str_buf.str();
  }

  bool SkipEmptyClass() const override { return true; }

  int NumModelPerIteration() const override { return num_class_; }

  int NumPredictOneRow() const override { return num_class_; }

  bool NeedAccuratePrediction() const override { return false; }

private:
  /*! \brief Number of data */
  data_size_t num_data_;
  /*! \brief Number of classes */
  int num_class_;
  /*! \brief Pointer of label */
  const float* label_;
  /*! \brief Corresponding integers of label_ */
  std::vector<int> label_int_;
  /*! \brief Weights for data */
  const float* weights_;
};

/*!
* \brief Objective function for multiclass classification, use one-vs-all binary objective function
*/
class MulticlassOVA: public ObjectiveFunction {
public:
  explicit MulticlassOVA(const ObjectiveConfig& config) {
    num_class_ = config.num_class;
    for (int i = 0; i < num_class_; ++i) {
      binary_loss_.emplace_back(
        new BinaryLogloss(config, [i](float label) { return static_cast<int>(label) == i; }));
    }
    sigmoid_ = config.sigmoid;
  }

  explicit MulticlassOVA(const std::vector<std::string>& strs) {
    num_class_ = -1;
    sigmoid_ = -1;
    for (auto str : strs) {
      auto tokens = Common::Split(str.c_str(), ':');
      if (tokens.size() == 2) {
        if (tokens[0] == std::string("num_class")) {
          Common::Atoi(tokens[1].c_str(), &num_class_);
        } else if (tokens[0] == std::string("sigmoid")) {
          Common::Atof(tokens[1].c_str(), &sigmoid_);
        }
      }
    }
    if (num_class_ < 0) {
      Log::Fatal("Objective should contains num_class field");
    }
    if (sigmoid_ <= 0.0) {
      Log::Fatal("Sigmoid parameter %f should be greater than zero", sigmoid_);
    }
  }

  ~MulticlassOVA() {

  }

  void Init(const Metadata& metadata, data_size_t num_data) override {
    num_data_ = num_data;
    for (int i = 0; i < num_class_; ++i) {
      binary_loss_[i]->Init(metadata, num_data);
    }
  }

  void GetGradients(const double* score, score_t* gradients, score_t* hessians) const override {
    for (int i = 0; i < num_class_; ++i) {
      int64_t bias = static_cast<int64_t>(num_data_) * i;
      binary_loss_[i]->GetGradients(score + bias, gradients + bias, hessians + bias);
    }
  }

  const char* GetName() const override {
    return "multiclassova";
  }

  void ConvertOutput(const double* input, double* output) const override {
    for (int i = 0; i < num_class_; ++i) {
      output[i] = 1.0f / (1.0f + std::exp(-sigmoid_ * input[i]));
    }
  }

  std::string ToString() const override {
    std::stringstream str_buf;
    str_buf << GetName() << " ";
    str_buf << "num_class:" << num_class_ << " ";
    str_buf << "sigmoid:" << sigmoid_;
    return str_buf.str();
  }

  bool SkipEmptyClass() const override { return true; }

  int NumModelPerIteration() const override { return num_class_; }

  int NumPredictOneRow() const override { return num_class_; }

  bool NeedAccuratePrediction() const override { return false; }

private:
  /*! \brief Number of data */
  data_size_t num_data_;
  /*! \brief Number of classes */
  int num_class_;
  std::vector<std::unique_ptr<BinaryLogloss>> binary_loss_;
  double sigmoid_;
};

}  // namespace LightGBM
#endif   // LightGBM_OBJECTIVE_MULTICLASS_OBJECTIVE_HPP_
