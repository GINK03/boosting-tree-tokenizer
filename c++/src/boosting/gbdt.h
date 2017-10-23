#ifndef LIGHTGBM_BOOSTING_GBDT_H_
#define LIGHTGBM_BOOSTING_GBDT_H_

#include <LightGBM/boosting.h>
#include <LightGBM/objective_function.h>
#include <LightGBM/prediction_early_stop.h>

#include "score_updater.hpp"

#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <memory>
#include <mutex>

namespace LightGBM {

/*!
* \brief GBDT algorithm implementation. including Training, prediction, bagging.
*/
class GBDT: public GBDTBase {
public:

  /*!
  * \brief Constructor
  */
  GBDT();

  /*!
  * \brief Destructor
  */
  ~GBDT();

  /*!
  * \brief Initialization logic
  * \param gbdt_config Config for boosting
  * \param train_data Training data
  * \param objective_function Training objective function
  * \param training_metrics Training metrics
  */
  void Init(const BoostingConfig* gbdt_config, const Dataset* train_data, const ObjectiveFunction* objective_function,
            const std::vector<const Metric*>& training_metrics) override;

  /*!
  * \brief Merge model from other boosting object. Will insert to the front of current boosting object
  * \param other
  */
  void MergeFrom(const Boosting* other) override {
    auto other_gbdt = reinterpret_cast<const GBDT*>(other);
    // tmp move to other vector
    auto original_models = std::move(models_);
    models_ = std::vector<std::unique_ptr<Tree>>();
    // push model from other first
    for (const auto& tree : other_gbdt->models_) {
      auto new_tree = std::unique_ptr<Tree>(new Tree(*(tree.get())));
      models_.push_back(std::move(new_tree));
    }
    num_init_iteration_ = static_cast<int>(models_.size()) / num_tree_per_iteration_;
    // push model in current object
    for (const auto& tree : original_models) {
      auto new_tree = std::unique_ptr<Tree>(new Tree(*(tree.get())));
      models_.push_back(std::move(new_tree));
    }
    num_iteration_for_pred_ = static_cast<int>(models_.size()) / num_tree_per_iteration_;
  }

  /*!
  * \brief Reset the training data
  * \param train_data New Training data
  * \param objective_function Training objective function
  * \param training_metrics Training metrics
  */
  void ResetTrainingData(const Dataset* train_data, const ObjectiveFunction* objective_function,
                         const std::vector<const Metric*>& training_metrics) override;

  /*!
  * \brief Reset Boosting Config
  * \param gbdt_config Config for boosting
  */
  void ResetConfig(const BoostingConfig* gbdt_config) override;

  /*!
  * \brief Adding a validation dataset
  * \param valid_data Validation dataset
  * \param valid_metrics Metrics for validation dataset
  */
  void AddValidDataset(const Dataset* valid_data,
                       const std::vector<const Metric*>& valid_metrics) override;

  /*!
  * \brief Perform a full training procedure
  * \param snapshot_freq frequence of snapshot
  * \param model_output_path path of model file
  */
  void Train(int snapshot_freq, const std::string& model_output_path) override;

  /*!
  * \brief Training logic
  * \param gradients nullptr for using default objective, otherwise use self-defined boosting
  * \param hessians nullptr for using default objective, otherwise use self-defined boosting
  * \return True if cannot train any more
  */
  virtual bool TrainOneIter(const score_t* gradients, const score_t* hessians) override;

  /*!
  * \brief Rollback one iteration
  */
  void RollbackOneIter() override;

  /*!
  * \brief Get current iteration
  */
  int GetCurrentIteration() const override { return static_cast<int>(models_.size()) / num_tree_per_iteration_; }

  /*!
  * \brief Can use early stopping for prediction or not
  * \return True if cannot use early stopping for prediction
  */
  bool NeedAccuratePrediction() const override {
    if (objective_function_ == nullptr) {
      return true;
    } else {
      return objective_function_->NeedAccuratePrediction();
    }
  }

  /*!
  * \brief Get evaluation result at data_idx data
  * \param data_idx 0: training data, 1: 1st validation data
  * \return evaluation result
  */
  std::vector<double> GetEvalAt(int data_idx) const override;

  /*!
  * \brief Get current training score
  * \param out_len length of returned score
  * \return training score
  */
  virtual const double* GetTrainingScore(int64_t* out_len) override;

  /*!
  * \brief Get size of prediction at data_idx data
  * \param data_idx 0: training data, 1: 1st validation data
  * \return The size of prediction
  */
  virtual int64_t GetNumPredictAt(int data_idx) const override {
    CHECK(data_idx >= 0 && data_idx <= static_cast<int>(valid_score_updater_.size()));
    data_size_t num_data = train_data_->num_data();
    if (data_idx > 0) {
      num_data = valid_score_updater_[data_idx - 1]->num_data();
    }
    return num_data * num_class_;
  }

  /*!
  * \brief Get prediction result at data_idx data
  * \param data_idx 0: training data, 1: 1st validation data
  * \param result used to store prediction result, should allocate memory before call this function
  * \param out_len length of returned score
  */
  void GetPredictAt(int data_idx, double* out_result, int64_t* out_len) override;

  /*!
  * \brief Get number of prediction for one data
  * \param num_iteration number of used iterations
  * \param is_pred_leaf True if predicting  leaf index
  * \param is_pred_contrib True if predicting feature contribution
  * \return number of prediction
  */
  inline int NumPredictOneRow(int num_iteration, bool is_pred_leaf, bool is_pred_contrib) const override {
    int num_preb_in_one_row = num_class_;
    if (is_pred_leaf) {
      int max_iteration = GetCurrentIteration();
      if (num_iteration > 0) {
        num_preb_in_one_row *= static_cast<int>(std::min(max_iteration, num_iteration));
      } else {
        num_preb_in_one_row *= max_iteration;
      }
    } else if (is_pred_contrib) {
      num_preb_in_one_row = max_feature_idx_ + 2; // +1 for 0-based indexing, +1 for baseline
    }
    return num_preb_in_one_row;
  }

  void PredictRaw(const double* features, double* output,
                  const PredictionEarlyStopInstance* earlyStop) const override;

  void Predict(const double* features, double* output,
               const PredictionEarlyStopInstance* earlyStop) const override;

  void PredictLeafIndex(const double* features, double* output) const override;

  void PredictContrib(const double* features, double* output,
                      const PredictionEarlyStopInstance* earlyStop) const override;

  /*!
  * \brief Dump model to json format string
  * \param num_iteration Number of iterations that want to dump, -1 means dump all
  * \return Json format string of model
  */
  std::string DumpModel(int num_iteration) const override;

  /*!
  * \brief Translate model to if-else statement
  * \param num_iteration Number of iterations that want to translate, -1 means translate all
  * \return if-else format codes of model
  */
  std::string ModelToIfElse(int num_iteration) const override;

  /*!
  * \brief Translate model to if-else statement
  * \param num_iteration Number of iterations that want to translate, -1 means translate all
  * \param filename Filename that want to save to
  * \return is_finish Is training finished or not
  */
  bool SaveModelToIfElse(int num_iteration, const char* filename) const override;

  /*!
  * \brief Save model to file
  * \param num_iterations Number of model that want to save, -1 means save all
  * \param filename Filename that want to save to
  * \return is_finish Is training finished or not
  */
  virtual bool SaveModelToFile(int num_iterations, const char* filename) const override;

  /*!
  * \brief Save model to string
  * \param num_iterations Number of model that want to save, -1 means save all
  * \return Non-empty string if succeeded
  */
  virtual std::string SaveModelToString(int num_iterations) const override;

  /*!
  * \brief Restore from a serialized string
  */
  bool LoadModelFromString(const std::string& model_str) override;

  /*!
  * \brief Calculate feature importances
  * \param num_iteration Number of model that want to use for feature importance, -1 means use all
  * \param importance_type: 0 for split, 1 for gain
  * \return vector of feature_importance
  */
  std::vector<double> FeatureImportance(int num_iteration, int importance_type) const override;

  /*!
  * \brief Get max feature index of this model
  * \return Max feature index of this model
  */
  inline int MaxFeatureIdx() const override { return max_feature_idx_; }

  /*!
  * \brief Get feature names of this model
  * \return Feature names of this model
  */
  inline std::vector<std::string> FeatureNames() const override { return feature_names_; }

  /*!
  * \brief Get index of label column
  * \return index of label column
  */
  inline int LabelIdx() const override { return label_idx_; }

  /*!
  * \brief Get number of weak sub-models
  * \return Number of weak sub-models
  */
  inline int NumberOfTotalModel() const override { return static_cast<int>(models_.size()); }

  /*!
  * \brief Get number of tree per iteration
  * \return number of tree per iteration
  */
  inline int NumModelPerIteration() const override { return num_tree_per_iteration_; }

  /*!
  * \brief Get number of classes
  * \return Number of classes
  */
  inline int NumberOfClasses() const override { return num_class_; }

  inline void InitPredict(int num_iteration) override {
    num_iteration_for_pred_ = static_cast<int>(models_.size()) / num_tree_per_iteration_;
    if (num_iteration > 0) {
      num_iteration_for_pred_ = std::min(num_iteration, num_iteration_for_pred_);
    }
  }

  inline double GetLeafValue(int tree_idx, int leaf_idx) const override {
    CHECK(tree_idx >= 0 && static_cast<size_t>(tree_idx) < models_.size());
    CHECK(leaf_idx >= 0 && leaf_idx < models_[tree_idx]->num_leaves());
    return models_[tree_idx]->LeafOutput(leaf_idx);
  }

  inline void SetLeafValue(int tree_idx, int leaf_idx, double val) override {
    CHECK(tree_idx >= 0 && static_cast<size_t>(tree_idx) < models_.size());
    CHECK(leaf_idx >= 0 && leaf_idx < models_[tree_idx]->num_leaves());
    models_[tree_idx]->SetLeafOutput(leaf_idx, val);
  }

  /*!
  * \brief Get Type name of this boosting object
  */
  virtual const char* SubModelName() const override { return "tree"; }

protected:

  /*!
  * \brief Print eval result and check early stopping
  */
  bool EvalAndCheckEarlyStopping();

  /*!
  * \brief reset config for bagging
  */
  void ResetBaggingConfig(const BoostingConfig* config, bool is_change_dataset);

  /*!
  * \brief Implement bagging logic
  * \param iter Current interation
  */
  virtual void Bagging(int iter);

  /*!
  * \brief Helper function for bagging, used for multi-threading optimization
  * \param start start indice of bagging
  * \param cnt count
  * \param buffer output buffer
  * \return count of left size
  */
  data_size_t BaggingHelper(Random& cur_rand, data_size_t start, data_size_t cnt, data_size_t* buffer);

  /*!
  * \brief calculate the object function
  */
  virtual void Boosting();

  /*!
  * \brief updating score after tree was trained
  * \param tree Trained tree of this iteration
  * \param cur_tree_id Current tree for multiclass training
  */
  virtual void UpdateScore(const Tree* tree, const int cur_tree_id);

  /*!
  * \brief eval results for one metric

  */
  virtual std::vector<double> EvalOneMetric(const Metric* metric, const double* score) const;

  /*!
  * \brief Print metric result of current iteration
  * \param iter Current interation
  * \return best_msg if met early_stopping
  */
  std::string OutputMetric(int iter);

  double BoostFromAverage();

  /*! \brief current iteration */
  int iter_;
  /*! \brief Pointer to training data */
  const Dataset* train_data_;
  /*! \brief Config of gbdt */
  std::unique_ptr<BoostingConfig> gbdt_config_;
  /*! \brief Tree learner, will use this class to learn trees */
  std::unique_ptr<TreeLearner> tree_learner_;
  /*! \brief Objective function */
  const ObjectiveFunction* objective_function_;
  /*! \brief Store and update training data's score */
  std::unique_ptr<ScoreUpdater> train_score_updater_;
  /*! \brief Metrics for training data */
  std::vector<const Metric*> training_metrics_;
  /*! \brief Store and update validation data's scores */
  std::vector<std::unique_ptr<ScoreUpdater>> valid_score_updater_;
  /*! \brief Metric for validation data */
  std::vector<std::vector<const Metric*>> valid_metrics_;
  /*! \brief Number of rounds for early stopping */
  int early_stopping_round_;
  /*! \brief Best iteration(s) for early stopping */
  std::vector<std::vector<int>> best_iter_;
  /*! \brief Best score(s) for early stopping */
  std::vector<std::vector<double>> best_score_;
  /*! \brief output message of best iteration */
  std::vector<std::vector<std::string>> best_msg_;
  /*! \brief Trained models(trees) */
  std::vector<std::unique_ptr<Tree>> models_;
  /*! \brief Max feature index of training data*/
  int max_feature_idx_;
  /*! \brief First order derivative of training data */
  std::vector<score_t> gradients_;
  /*! \brief Secend order derivative of training data */
  std::vector<score_t> hessians_;
  /*! \brief Store the indices of in-bag data */
  std::vector<data_size_t> bag_data_indices_;
  /*! \brief Number of in-bag data */
  data_size_t bag_data_cnt_;
  /*! \brief Store the indices of in-bag data */
  std::vector<data_size_t> tmp_indices_;
  /*! \brief Number of training data */
  data_size_t num_data_;
  /*! \brief Number of trees per iterations */
  int num_tree_per_iteration_;
  /*! \brief Number of class */
  int num_class_;
  /*! \brief Index of label column */
  data_size_t label_idx_;
  /*! \brief number of used model */
  int num_iteration_for_pred_;
  /*! \brief Shrinkage rate for one iteration */
  double shrinkage_rate_;
  /*! \brief Number of loaded initial models */
  int num_init_iteration_;
  /*! \brief Feature names */
  std::vector<std::string> feature_names_;
  std::vector<std::string> feature_infos_;
  /*! \brief number of threads */
  int num_threads_;
  /*! \brief Buffer for multi-threading bagging */
  std::vector<data_size_t> offsets_buf_;
  /*! \brief Buffer for multi-threading bagging */
  std::vector<data_size_t> left_cnts_buf_;
  /*! \brief Buffer for multi-threading bagging */
  std::vector<data_size_t> right_cnts_buf_;
  /*! \brief Buffer for multi-threading bagging */
  std::vector<data_size_t> left_write_pos_buf_;
  /*! \brief Buffer for multi-threading bagging */
  std::vector<data_size_t> right_write_pos_buf_;
  std::unique_ptr<Dataset> tmp_subset_;
  bool is_use_subset_;
  std::vector<bool> class_need_train_;
  std::vector<double> class_default_output_;
  bool is_constant_hessian_;
  std::unique_ptr<ObjectiveFunction> loaded_objective_;
  bool average_output_;
  bool need_re_bagging_;
};

}  // namespace LightGBM
#endif   // LightGBM_BOOSTING_GBDT_H_
