#ifndef LIGHTGBM_METRIC_REGRESSION_METRIC_HPP_
#define LIGHTGBM_METRIC_REGRESSION_METRIC_HPP_

#include <LightGBM/utils/log.h>

#include <LightGBM/metric.h>

#include <cmath>

namespace LightGBM {
/*!
* \brief Metric for regression task.
* Use static class "PointWiseLossCalculator" to calculate loss point-wise
*/
template<typename PointWiseLossCalculator>
class RegressionMetric: public Metric {
public:
  explicit RegressionMetric(const MetricConfig&) :huber_delta_(1.0f), fair_c_(1.0f) {
  }

  virtual ~RegressionMetric() {

  }

  const std::vector<std::string>& GetName() const override {
    return name_;
  }

  double factor_to_bigger_better() const override {
    return -1.0f;
  }

  void Init(const Metadata& metadata, data_size_t num_data) override {
    name_.emplace_back(PointWiseLossCalculator::Name());

    num_data_ = num_data;
    // get label
    label_ = metadata.label();
    // get weights
    weights_ = metadata.weights();
    if (weights_ == nullptr) {
      sum_weights_ = static_cast<double>(num_data_);
    } else {
      sum_weights_ = 0.0f;
      for (data_size_t i = 0; i < num_data_; ++i) {
        sum_weights_ += weights_[i];
      }
    }
  }

  std::vector<double> Eval(const double* score, const ObjectiveFunction* objective) const override {
    double sum_loss = 0.0f;
    if (objective == nullptr) {
      if (weights_ == nullptr) {
        #pragma omp parallel for schedule(static) reduction(+:sum_loss)
        for (data_size_t i = 0; i < num_data_; ++i) {
          // add loss
          sum_loss += PointWiseLossCalculator::LossOnPoint(label_[i], score[i], huber_delta_, fair_c_);
        }
      } else {
        #pragma omp parallel for schedule(static) reduction(+:sum_loss)
        for (data_size_t i = 0; i < num_data_; ++i) {
          // add loss
          sum_loss += PointWiseLossCalculator::LossOnPoint(label_[i], score[i], huber_delta_, fair_c_) * weights_[i];
        }
      }
    } else {
      if (weights_ == nullptr) {
        #pragma omp parallel for schedule(static) reduction(+:sum_loss)
        for (data_size_t i = 0; i < num_data_; ++i) {
          // add loss
          double t = 0;
          objective->ConvertOutput(&score[i], &t);
          sum_loss += PointWiseLossCalculator::LossOnPoint(label_[i], t, huber_delta_, fair_c_);
        }
      } else {
        #pragma omp parallel for schedule(static) reduction(+:sum_loss)
        for (data_size_t i = 0; i < num_data_; ++i) {
          // add loss
          double t = 0;
          objective->ConvertOutput(&score[i], &t);
          sum_loss += PointWiseLossCalculator::LossOnPoint(label_[i], t, huber_delta_, fair_c_) * weights_[i];
        }
      }
    }
    double loss = PointWiseLossCalculator::AverageLoss(sum_loss, sum_weights_);
    return std::vector<double>(1, loss);

  }

  inline static double AverageLoss(double sum_loss, double sum_weights) {
    return sum_loss / sum_weights;
  }

protected:
  /*! \brief delta for Huber loss */
  double huber_delta_;
  /*! \brief c for Fair loss */
  double fair_c_;

private:
  /*! \brief Number of data */
  data_size_t num_data_;
  /*! \brief Pointer of label */
  const float* label_;
  /*! \brief Pointer of weighs */
  const float* weights_;
  /*! \brief Sum weights */
  double sum_weights_;
  /*! \brief Name of this test set */
  std::vector<std::string> name_;
};

/*! \brief RMSE loss for regression task */
class RMSEMetric: public RegressionMetric<RMSEMetric> {
public:
  explicit RMSEMetric(const MetricConfig& config) :RegressionMetric<RMSEMetric>(config) {}

  inline static double LossOnPoint(float label, double score, double, double) {
    return (score - label)*(score - label);
  }

  inline static double AverageLoss(double sum_loss, double sum_weights) {
    // need sqrt the result for RMSE loss
    return std::sqrt(sum_loss / sum_weights);
  }

  inline static const char* Name() {
    return "rmse";
  }
};

/*! \brief L2 loss for regression task */
class L2Metric: public RegressionMetric<L2Metric> {
public:
  explicit L2Metric(const MetricConfig& config) :RegressionMetric<L2Metric>(config) {}

  inline static double LossOnPoint(float label, double score, double, double) {
    return (score - label)*(score - label);
  }

  inline static double AverageLoss(double sum_loss, double sum_weights) {
    // need mean of the result for L2 loss
    return sum_loss / sum_weights;
  }

  inline static const char* Name() {
    return "l2";
  }
};

/*! \brief L1 loss for regression task */
class L1Metric: public RegressionMetric<L1Metric> {
public:
  explicit L1Metric(const MetricConfig& config) :RegressionMetric<L1Metric>(config) {}

  inline static double LossOnPoint(float label, double score, double, double) {
    return std::fabs(score - label);
  }
  inline static const char* Name() {
    return "l1";
  }
};

/*! \brief Huber loss for regression task */
class HuberLossMetric: public RegressionMetric<HuberLossMetric> {
public:
  explicit HuberLossMetric(const MetricConfig& config) :RegressionMetric<HuberLossMetric>(config) {
    huber_delta_ = static_cast<double>(config.huber_delta);
  }

  inline static double LossOnPoint(float label, double score, double delta, double) {
    const double diff = score - label;
    if (std::abs(diff) <= delta) {
      return 0.5f * diff * diff;
    } else {
      return delta * (std::abs(diff) - 0.5f * delta);
    }
  }

  inline static const char* Name() {
    return "huber";
  }
};

/*! \brief Fair loss for regression task */
// http://research.microsoft.com/en-us/um/people/zhang/INRIA/Publis/Tutorial-Estim/node24.html
class FairLossMetric: public RegressionMetric<FairLossMetric> {
public:
  explicit FairLossMetric(const MetricConfig& config) :RegressionMetric<FairLossMetric>(config) {
    fair_c_ = static_cast<double>(config.fair_c);
  }

  inline static double LossOnPoint(float label, double score, double, double c) {
    const double x = std::fabs(score - label);
    return c * x - c * c * std::log(1.0f + x / c);
  }

  inline static const char* Name() {
    return "fair";
  }
};

/*! \brief Poisson regression loss for regression task */
class PoissonMetric: public RegressionMetric<PoissonMetric> {
public:
  explicit PoissonMetric(const MetricConfig& config) :RegressionMetric<PoissonMetric>(config) {
  }

  inline static double LossOnPoint(float label, double score, double, double) {
    const double eps = 1e-10f;
    if (score < eps) {
      score = eps;
    }
    return score - label * std::log(score);
  }
  inline static const char* Name() {
    return "poisson";
  }
};

}  // namespace LightGBM
#endif   // LightGBM_METRIC_REGRESSION_METRIC_HPP_
