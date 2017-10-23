#include <LightGBM/utils/openmp_wrapper.h>

#include <LightGBM/utils/log.h>
#include <LightGBM/dataset_loader.h>
#include <LightGBM/network.h>


namespace LightGBM {

DatasetLoader::DatasetLoader(const IOConfig& io_config, const PredictFunction& predict_fun, int num_class, const char* filename)
  :io_config_(io_config), random_(io_config_.data_random_seed), predict_fun_(predict_fun), num_class_(num_class) {
  label_idx_ = 0;
  weight_idx_ = NO_SPECIFIC;
  group_idx_ = NO_SPECIFIC;
  SetHeader(filename);
}

DatasetLoader::~DatasetLoader() {

}

void DatasetLoader::SetHeader(const char* filename) {
  std::unordered_map<std::string, int> name2idx;
  std::string name_prefix("name:");
  if (filename != nullptr) {
    TextReader<data_size_t> text_reader(filename, io_config_.has_header);

    // get column names
    if (io_config_.has_header) {
      std::string first_line = text_reader.first_line();
      feature_names_ = Common::Split(first_line.c_str(), "\t,");
    }

    // load label idx first
    if (io_config_.label_column.size() > 0) {
      if (Common::StartsWith(io_config_.label_column, name_prefix)) {
        std::string name = io_config_.label_column.substr(name_prefix.size());
        label_idx_ = -1;
        for (int i = 0; i < static_cast<int>(feature_names_.size()); ++i) {
          if (name == feature_names_[i]) {
            label_idx_ = i;
            break;
          }
        }
        if (label_idx_ >= 0) {
          Log::Info("Using column %s as label", name.c_str());
        } else {
          Log::Fatal("Could not find label column %s in data file \
                      or data file doesn't contain header", name.c_str());
        }
      } else {
        if (!Common::AtoiAndCheck(io_config_.label_column.c_str(), &label_idx_)) {
          Log::Fatal("label_column is not a number, \
                      if you want to use a column name, \
                      please add the prefix \"name:\" to the column name");
        }
        Log::Info("Using column number %d as label", label_idx_);
      }
    }

    if (!feature_names_.empty()) {
      // erase label column name
      feature_names_.erase(feature_names_.begin() + label_idx_);
      for (size_t i = 0; i < feature_names_.size(); ++i) {
        name2idx[feature_names_[i]] = static_cast<int>(i);
      }
    }

    // load ignore columns
    if (io_config_.ignore_column.size() > 0) {
      if (Common::StartsWith(io_config_.ignore_column, name_prefix)) {
        std::string names = io_config_.ignore_column.substr(name_prefix.size());
        for (auto name : Common::Split(names.c_str(), ',')) {
          if (name2idx.count(name) > 0) {
            int tmp = name2idx[name];
            ignore_features_.emplace(tmp);
          } else {
            Log::Fatal("Could not find ignore column %s in data file", name.c_str());
          }
        }
      } else {
        for (auto token : Common::Split(io_config_.ignore_column.c_str(), ',')) {
          int tmp = 0;
          if (!Common::AtoiAndCheck(token.c_str(), &tmp)) {
            Log::Fatal("ignore_column is not a number, \
                        if you want to use a column name, \
                        please add the prefix \"name:\" to the column name");
          }
          ignore_features_.emplace(tmp);
        }
      }
    }
    // load weight idx
    if (io_config_.weight_column.size() > 0) {
      if (Common::StartsWith(io_config_.weight_column, name_prefix)) {
        std::string name = io_config_.weight_column.substr(name_prefix.size());
        if (name2idx.count(name) > 0) {
          weight_idx_ = name2idx[name];
          Log::Info("Using column %s as weight", name.c_str());
        } else {
          Log::Fatal("Could not find weight column %s in data file", name.c_str());
        }
      } else {
        if (!Common::AtoiAndCheck(io_config_.weight_column.c_str(), &weight_idx_)) {
          Log::Fatal("weight_column is not a number, \
                      if you want to use a column name, \
                      please add the prefix \"name:\" to the column name");
        }
        Log::Info("Using column number %d as weight", weight_idx_);
      }
      ignore_features_.emplace(weight_idx_);
    }
    // load group idx
    if (io_config_.group_column.size() > 0) {
      if (Common::StartsWith(io_config_.group_column, name_prefix)) {
        std::string name = io_config_.group_column.substr(name_prefix.size());
        if (name2idx.count(name) > 0) {
          group_idx_ = name2idx[name];
          Log::Info("Using column %s as group/query id", name.c_str());
        } else {
          Log::Fatal("Could not find group/query column %s in data file", name.c_str());
        }
      } else {
        if (!Common::AtoiAndCheck(io_config_.group_column.c_str(), &group_idx_)) {
          Log::Fatal("group_column is not a number, \
                      if you want to use a column name, \
                      please add the prefix \"name:\" to the column name");
        }
        Log::Info("Using column number %d as group/query id", group_idx_);
      }
      ignore_features_.emplace(group_idx_);
    }
  }
  if (io_config_.categorical_column.size() > 0) {
    if (Common::StartsWith(io_config_.categorical_column, name_prefix)) {
      std::string names = io_config_.categorical_column.substr(name_prefix.size());
      for (auto name : Common::Split(names.c_str(), ',')) {
        if (name2idx.count(name) > 0) {
          int tmp = name2idx[name];
          categorical_features_.emplace(tmp);
        } else {
          Log::Fatal("Could not find categorical_column %s in data file", name.c_str());
        }
      }
    } else {
      for (auto token : Common::Split(io_config_.categorical_column.c_str(), ',')) {
        int tmp = 0;
        if (!Common::AtoiAndCheck(token.c_str(), &tmp)) {
          Log::Fatal("categorical_column is not a number, \
                        if you want to use a column name, \
                        please add the prefix \"name:\" to the column name");
        }
        categorical_features_.emplace(tmp);
      }
    }
  }
}

Dataset* DatasetLoader::LoadFromFile(const char* filename, const char* initscore_file, int rank, int num_machines) {
  // don't support query id in data file when training in parallel
  if (num_machines > 1 && !io_config_.is_pre_partition) {
    if (group_idx_ > 0) {
      Log::Fatal("Using a query id without pre-partitioning the data file is not supported for parallel training. \
                  Please use an additional query file or pre-partition the data");
    }
  }
  auto dataset = std::unique_ptr<Dataset>(new Dataset());
  data_size_t num_global_data = 0;
  std::vector<data_size_t> used_data_indices;
  auto bin_filename = CheckCanLoadFromBin(filename);
  if (bin_filename.size() == 0) {
    auto parser = std::unique_ptr<Parser>(Parser::CreateParser(filename, io_config_.has_header, 0, label_idx_));
    if (parser == nullptr) {
      Log::Fatal("Could not recognize data format of %s", filename);
    }
    dataset->data_filename_ = filename;
    dataset->label_idx_ = label_idx_;
    dataset->metadata_.Init(filename, initscore_file);
    if (!io_config_.use_two_round_loading) {
      // read data to memory
      auto text_data = LoadTextDataToMemory(filename, dataset->metadata_, rank, num_machines, &num_global_data, &used_data_indices);
      dataset->num_data_ = static_cast<data_size_t>(text_data.size());
      // sample data
      auto sample_data = SampleTextDataFromMemory(text_data);
      // construct feature bin mappers
      ConstructBinMappersFromTextData(rank, num_machines, sample_data, parser.get(), dataset.get());
      // initialize label
      dataset->metadata_.Init(dataset->num_data_, weight_idx_, group_idx_);
      // extract features
      ExtractFeaturesFromMemory(text_data, parser.get(), dataset.get());
      text_data.clear();
    } else {
      // sample data from file
      auto sample_data = SampleTextDataFromFile(filename, dataset->metadata_, rank, num_machines, &num_global_data, &used_data_indices);
      if (used_data_indices.size() > 0) {
        dataset->num_data_ = static_cast<data_size_t>(used_data_indices.size());
      } else {
        dataset->num_data_ = num_global_data;
      }
      // construct feature bin mappers
      ConstructBinMappersFromTextData(rank, num_machines, sample_data, parser.get(), dataset.get());
      // initialize label
      dataset->metadata_.Init(dataset->num_data_, weight_idx_, group_idx_);

      // extract features
      ExtractFeaturesFromFile(filename, parser.get(), used_data_indices, dataset.get());
    }
  } else {
    // load data from binary file
    dataset.reset(LoadFromBinFile(filename, bin_filename.c_str(), rank, num_machines, &num_global_data, &used_data_indices));
  }
  // check meta data
  dataset->metadata_.CheckOrPartition(num_global_data, used_data_indices);
  // need to check training data
  CheckDataset(dataset.get());
  return dataset.release();
}



Dataset* DatasetLoader::LoadFromFileAlignWithOtherDataset(const char* filename, const char* initscore_file, const Dataset* train_data) {
  data_size_t num_global_data = 0;
  std::vector<data_size_t> used_data_indices;
  auto dataset = std::unique_ptr<Dataset>(new Dataset());
  auto bin_filename = CheckCanLoadFromBin(filename);
  if (bin_filename.size() == 0) {
    auto parser = std::unique_ptr<Parser>(Parser::CreateParser(filename, io_config_.has_header, 0, label_idx_));
    if (parser == nullptr) {
      Log::Fatal("Could not recognize data format of %s", filename);
    }
    dataset->data_filename_ = filename;
    dataset->label_idx_ = label_idx_;
    dataset->metadata_.Init(filename, initscore_file);
    if (!io_config_.use_two_round_loading) {
      // read data in memory
      auto text_data = LoadTextDataToMemory(filename, dataset->metadata_, 0, 1, &num_global_data, &used_data_indices);
      dataset->num_data_ = static_cast<data_size_t>(text_data.size());
      // initialize label
      dataset->metadata_.Init(dataset->num_data_, weight_idx_, group_idx_);
      dataset->CreateValid(train_data);
      // extract features
      ExtractFeaturesFromMemory(text_data, parser.get(), dataset.get());
      text_data.clear();
    } else {
      TextReader<data_size_t> text_reader(filename, io_config_.has_header);
      // Get number of lines of data file
      dataset->num_data_ = static_cast<data_size_t>(text_reader.CountLine());
      num_global_data = dataset->num_data_;
      // initialize label
      dataset->metadata_.Init(dataset->num_data_, weight_idx_, group_idx_);
      dataset->CreateValid(train_data);
      // extract features
      ExtractFeaturesFromFile(filename, parser.get(), used_data_indices, dataset.get());
    }
  } else {
    // load data from binary file
    dataset.reset(LoadFromBinFile(filename, bin_filename.c_str(), 0, 1, &num_global_data, &used_data_indices));
  }
  // not need to check validation data
  // check meta data
  dataset->metadata_.CheckOrPartition(num_global_data, used_data_indices);
  return dataset.release();
}

Dataset* DatasetLoader::LoadFromBinFile(const char* data_filename, const char* bin_filename, int rank, int num_machines, int* num_global_data, std::vector<data_size_t>* used_data_indices) {
  auto dataset = std::unique_ptr<Dataset>(new Dataset());
  FILE* file;
  #ifdef _MSC_VER
  fopen_s(&file, bin_filename, "rb");
  #else
  file = fopen(bin_filename, "rb");
  #endif
  dataset->data_filename_ = data_filename;
  if (file == NULL) {
    Log::Fatal("Could not read binary data from %s", bin_filename);
  }

  // buffer to read binary file
  size_t buffer_size = 16 * 1024 * 1024;
  auto buffer = std::vector<char>(buffer_size);

  // check token
  size_t size_of_token = std::strlen(Dataset::binary_file_token);
  size_t read_cnt = fread(buffer.data(), sizeof(char), size_of_token, file);
  if (read_cnt != size_of_token) {
    Log::Fatal("Binary file error: token has the wrong size");
  }
  if (std::string(buffer.data()) != std::string(Dataset::binary_file_token)) {
    Log::Fatal("input file is not LightGBM binary file");
  }

  // read size of header
  read_cnt = fread(buffer.data(), sizeof(size_t), 1, file);

  if (read_cnt != 1) {
    Log::Fatal("Binary file error: header has the wrong size");
  }

  size_t size_of_head = *(reinterpret_cast<size_t*>(buffer.data()));

  // re-allocmate space if not enough
  if (size_of_head > buffer_size) {
    buffer_size = size_of_head;
    buffer.resize(buffer_size);
  }
  // read header
  read_cnt = fread(buffer.data(), 1, size_of_head, file);

  if (read_cnt != size_of_head) {
    Log::Fatal("Binary file error: header is incorrect");
  }
  // get header
  const char* mem_ptr = buffer.data();
  dataset->num_data_ = *(reinterpret_cast<const data_size_t*>(mem_ptr));
  mem_ptr += sizeof(dataset->num_data_);
  dataset->num_features_ = *(reinterpret_cast<const int*>(mem_ptr));
  mem_ptr += sizeof(dataset->num_features_);
  dataset->num_total_features_ = *(reinterpret_cast<const int*>(mem_ptr));
  mem_ptr += sizeof(dataset->num_total_features_);
  dataset->label_idx_ = *(reinterpret_cast<const int*>(mem_ptr));
  mem_ptr += sizeof(dataset->label_idx_);
  const int* tmp_feature_map = reinterpret_cast<const int*>(mem_ptr);
  dataset->used_feature_map_.clear();
  for (int i = 0; i < dataset->num_total_features_; ++i) {
    dataset->used_feature_map_.push_back(tmp_feature_map[i]);
  }
  mem_ptr += sizeof(int) * dataset->num_total_features_;
  // num_groups
  dataset->num_groups_ = *(reinterpret_cast<const int*>(mem_ptr));
  mem_ptr += sizeof(dataset->num_groups_);
  // real_feature_idx_
  const int* tmp_ptr_real_feature_idx_ = reinterpret_cast<const int*>(mem_ptr);
  dataset->real_feature_idx_.clear();
  for (int i = 0; i < dataset->num_features_; ++i) {
    dataset->real_feature_idx_.push_back(tmp_ptr_real_feature_idx_[i]);
  }
  mem_ptr += sizeof(int) * dataset->num_features_;
  // feature2group
  const int* tmp_ptr_feature2group = reinterpret_cast<const int*>(mem_ptr);
  dataset->feature2group_.clear();
  for (int i = 0; i < dataset->num_features_; ++i) {
    dataset->feature2group_.push_back(tmp_ptr_feature2group[i]);
  }
  mem_ptr += sizeof(int) * dataset->num_features_;
  // feature2subfeature
  const int* tmp_ptr_feature2subfeature = reinterpret_cast<const int*>(mem_ptr);
  dataset->feature2subfeature_.clear();
  for (int i = 0; i < dataset->num_features_; ++i) {
    dataset->feature2subfeature_.push_back(tmp_ptr_feature2subfeature[i]);
  }
  mem_ptr += sizeof(int) * dataset->num_features_;
  // group_bin_boundaries
  const uint64_t* tmp_ptr_group_bin_boundaries = reinterpret_cast<const uint64_t*>(mem_ptr);
  dataset->group_bin_boundaries_.clear();
  for (int i = 0; i < dataset->num_groups_ + 1; ++i) {
    dataset->group_bin_boundaries_.push_back(tmp_ptr_group_bin_boundaries[i]);
  }
  mem_ptr += sizeof(uint64_t) * (dataset->num_groups_ + 1);

  // group_feature_start_
  const int* tmp_ptr_group_feature_start = reinterpret_cast<const int*>(mem_ptr);
  dataset->group_feature_start_.clear();
  for (int i = 0; i < dataset->num_groups_; ++i) {
    dataset->group_feature_start_.push_back(tmp_ptr_group_feature_start[i]);
  }
  mem_ptr += sizeof(int) * (dataset->num_groups_);

  // group_feature_cnt_
  const int* tmp_ptr_group_feature_cnt = reinterpret_cast<const int*>(mem_ptr);
  dataset->group_feature_cnt_.clear();
  for (int i = 0; i < dataset->num_groups_; ++i) {
    dataset->group_feature_cnt_.push_back(tmp_ptr_group_feature_cnt[i]);
  }
  mem_ptr += sizeof(int) * (dataset->num_groups_);

  // get feature names
  dataset->feature_names_.clear();
  // write feature names
  for (int i = 0; i < dataset->num_total_features_; ++i) {
    int str_len = *(reinterpret_cast<const int*>(mem_ptr));
    mem_ptr += sizeof(int);
    std::stringstream str_buf;
    for (int j = 0; j < str_len; ++j) {
      char tmp_char = *(reinterpret_cast<const char*>(mem_ptr));
      mem_ptr += sizeof(char);
      str_buf << tmp_char;
    }
    dataset->feature_names_.emplace_back(str_buf.str());
  }

  // read size of meta data
  read_cnt = fread(buffer.data(), sizeof(size_t), 1, file);

  if (read_cnt != 1) {
    Log::Fatal("Binary file error: meta data has the wrong size");
  }

  size_t size_of_metadata = *(reinterpret_cast<size_t*>(buffer.data()));

  // re-allocate space if not enough
  if (size_of_metadata > buffer_size) {
    buffer_size = size_of_metadata;
    buffer.resize(buffer_size);
  }
  //  read meta data
  read_cnt = fread(buffer.data(), 1, size_of_metadata, file);

  if (read_cnt != size_of_metadata) {
    Log::Fatal("Binary file error: meta data is incorrect");
  }
  // load meta data
  dataset->metadata_.LoadFromMemory(buffer.data());

  *num_global_data = dataset->num_data_;
  used_data_indices->clear();
  // sample local used data if need to partition
  if (num_machines > 1 && !io_config_.is_pre_partition) {
    const data_size_t* query_boundaries = dataset->metadata_.query_boundaries();
    if (query_boundaries == nullptr) {
      // if not contain query file, minimal sample unit is one record
      for (data_size_t i = 0; i < dataset->num_data_; ++i) {
        if (random_.NextShort(0, num_machines) == rank) {
          used_data_indices->push_back(i);
        }
      }
    } else {
      // if contain query file, minimal sample unit is one query
      data_size_t num_queries = dataset->metadata_.num_queries();
      data_size_t qid = -1;
      bool is_query_used = false;
      for (data_size_t i = 0; i < dataset->num_data_; ++i) {
        if (qid >= num_queries) {
          Log::Fatal("Current query exceeds the range of the query file, please ensure the query file is correct");
        }
        if (i >= query_boundaries[qid + 1]) {
          // if is new query
          is_query_used = false;
          if (random_.NextShort(0, num_machines) == rank) {
            is_query_used = true;
          }
          ++qid;
        }
        if (is_query_used) {
          used_data_indices->push_back(i);
        }
      }
    }
    dataset->num_data_ = static_cast<data_size_t>((*used_data_indices).size());
  }
  dataset->metadata_.PartitionLabel(*used_data_indices);
  // read feature data
  for (int i = 0; i < dataset->num_groups_; ++i) {
    // read feature size
    read_cnt = fread(buffer.data(), sizeof(size_t), 1, file);
    if (read_cnt != 1) {
      Log::Fatal("Binary file error: feature %d has the wrong size", i);
    }
    size_t size_of_feature = *(reinterpret_cast<size_t*>(buffer.data()));
    // re-allocate space if not enough
    if (size_of_feature > buffer_size) {
      buffer_size = size_of_feature;
      buffer.resize(buffer_size);
    }

    read_cnt = fread(buffer.data(), 1, size_of_feature, file);

    if (read_cnt != size_of_feature) {
      Log::Fatal("Binary file error: feature %d is incorrect, read count: %d", i, read_cnt);
    }
    dataset->feature_groups_.emplace_back(std::unique_ptr<FeatureGroup>(
      new FeatureGroup(buffer.data(),
                       *num_global_data,
                       *used_data_indices)
      ));
  }
  dataset->feature_groups_.shrink_to_fit();
  fclose(file);
  dataset->is_finish_load_ = true;
  return dataset.release();
}

Dataset* DatasetLoader::CostructFromSampleData(double** sample_values,
                                               int** sample_indices, int num_col, const int* num_per_col,
                                               size_t total_sample_size, data_size_t num_data) {
  std::vector<std::unique_ptr<BinMapper>> bin_mappers(num_col);
  // fill feature_names_ if not header
  if (feature_names_.empty()) {
    for (int i = 0; i < num_col; ++i) {
      std::stringstream str_buf;
      str_buf << "Column_" << i;
      feature_names_.push_back(str_buf.str());
    }
  }

  const data_size_t filter_cnt = static_cast<data_size_t>(
    static_cast<double>(io_config_.min_data_in_leaf * total_sample_size) / num_data);
  if (Network::num_machines() == 1) {
    // if only one machine, find bin locally
    OMP_INIT_EX();
    #pragma omp parallel for schedule(guided)
    for (int i = 0; i < num_col; ++i) {
      OMP_LOOP_EX_BEGIN();
      if (ignore_features_.count(i) > 0) {
        bin_mappers[i] = nullptr;
        continue;
      }
      BinType bin_type = BinType::NumericalBin;
      if (categorical_features_.count(i)) {
        bin_type = BinType::CategoricalBin;
      }
      bin_mappers[i].reset(new BinMapper());
      bin_mappers[i]->FindBin(sample_values[i], num_per_col[i], total_sample_size,
                              io_config_.max_bin, io_config_.min_data_in_bin, filter_cnt, bin_type, io_config_.use_missing, io_config_.zero_as_missing);
      OMP_LOOP_EX_END();
    }
    OMP_THROW_EX();
  } else {
    // if have multi-machines, need to find bin distributed
    // different machines will find bin for different features
    int num_machines = Network::num_machines();
    int rank = Network::rank();
    int total_num_feature = num_col;
    total_num_feature = Network::GlobalSyncUpByMin(total_num_feature);
    // start and len will store the process feature indices for different machines
    // machine i will find bins for features in [ start[i], start[i] + len[i] )
    std::vector<int> start(num_machines);
    std::vector<int> len(num_machines);
    int step = (total_num_feature + num_machines - 1) / num_machines;
    if (step < 1) { step = 1; }

    start[0] = 0;
    for (int i = 0; i < num_machines - 1; ++i) {
      len[i] = std::min(step, total_num_feature - start[i]);
      start[i + 1] = start[i] + len[i];
    }
    len[num_machines - 1] = total_num_feature - start[num_machines - 1];
    OMP_INIT_EX();
    #pragma omp parallel for schedule(guided)
    for (int i = 0; i < len[rank]; ++i) {
      OMP_LOOP_EX_BEGIN();
      if (ignore_features_.count(start[rank] + i) > 0) {
        continue;
      }
      BinType bin_type = BinType::NumericalBin;
      if (categorical_features_.count(start[rank] + i)) {
        bin_type = BinType::CategoricalBin;
      }
      bin_mappers[i].reset(new BinMapper());
      bin_mappers[i]->FindBin(sample_values[start[rank] + i], num_per_col[start[rank] + i], total_sample_size,
                              io_config_.max_bin, io_config_.min_data_in_bin, filter_cnt, bin_type, io_config_.use_missing, io_config_.zero_as_missing);
      OMP_LOOP_EX_END();
    }
    OMP_THROW_EX();
    int max_bin = 0;
    for (int i = 0; i < len[rank]; ++i) {
      if (bin_mappers[i] != nullptr) {
        max_bin = std::max(max_bin, bin_mappers[i]->num_bin());
      }
    }
    max_bin = Network::GlobalSyncUpByMax(max_bin);
    // get size of bin mapper with max_bin size
    int type_size = BinMapper::SizeForSpecificBin(max_bin);
    // since sizes of different feature may not be same, we expand all bin mapper to type_size
    int buffer_size = type_size * total_num_feature;
    auto input_buffer = std::vector<char>(buffer_size);
    auto output_buffer = std::vector<char>(buffer_size);

    // find local feature bins and copy to buffer
    #pragma omp parallel for schedule(guided)
    for (int i = 0; i < len[rank]; ++i) {
      OMP_LOOP_EX_BEGIN();
      if (ignore_features_.count(start[rank] + i) > 0) {
        continue;
      }
      bin_mappers[i]->CopyTo(input_buffer.data() + i * type_size);
      // free
      bin_mappers[i].reset(nullptr);
      OMP_LOOP_EX_END();
    }
    OMP_THROW_EX();
    // convert to binary size
    for (int i = 0; i < num_machines; ++i) {
      start[i] *= type_size;
      len[i] *= type_size;
    }
    // gather global feature bin mappers
    Network::Allgather(input_buffer.data(), buffer_size, start.data(), len.data(), output_buffer.data());
    // restore features bins from buffer
    for (int i = 0; i < total_num_feature; ++i) {
      if (ignore_features_.count(i) > 0) {
        bin_mappers[i] = nullptr;
        continue;
      }
      bin_mappers[i].reset(new BinMapper());
      bin_mappers[i]->CopyFrom(output_buffer.data() + i * type_size);
    }
  }
  auto dataset = std::unique_ptr<Dataset>(new Dataset(num_data));
  dataset->Construct(bin_mappers, sample_indices, num_per_col, total_sample_size, io_config_);
  dataset->set_feature_names(feature_names_);
  return dataset.release();
}


// ---- private functions ----

void DatasetLoader::CheckDataset(const Dataset* dataset) {
  if (dataset->num_data_ <= 0) {
    Log::Fatal("Data file %s is empty", dataset->data_filename_);
  }
  if (dataset->feature_groups_.empty()) {
    Log::Fatal("No usable features in data file %s", dataset->data_filename_);
  }
  if (dataset->feature_names_.size() != static_cast<size_t>(dataset->num_total_features_)) {
    Log::Fatal("Size of feature name error, should be %d, got %d", dataset->num_total_features_,
               static_cast<int>(dataset->feature_names_.size()));
  }
  bool is_feature_order_by_group = true;
  int last_group = -1;
  int last_sub_feature = -1;
  // if features are ordered, not need to use hist_buf
  for (int i = 0; i < dataset->num_features_; ++i) {
    int group = dataset->feature2group_[i];
    int sub_feature = dataset->feature2subfeature_[i];
    if (group < last_group) {
      is_feature_order_by_group = false;
    } else if (group == last_group) {
      if (sub_feature <= last_sub_feature) {
        is_feature_order_by_group = false;
        break;
      }
    }
    last_group = group;
    last_sub_feature = sub_feature;
  }
  if (!is_feature_order_by_group) {
    Log::Fatal("feature in dataset should order by group");
  }
}

std::vector<std::string> DatasetLoader::LoadTextDataToMemory(const char* filename, const Metadata& metadata,
                                                             int rank, int num_machines, int* num_global_data,
                                                             std::vector<data_size_t>* used_data_indices) {
  TextReader<data_size_t> text_reader(filename, io_config_.has_header);
  used_data_indices->clear();
  if (num_machines == 1 || io_config_.is_pre_partition) {
    // read all lines
    *num_global_data = text_reader.ReadAllLines();
  } else {  // need partition data
            // get query data
    const data_size_t* query_boundaries = metadata.query_boundaries();

    if (query_boundaries == nullptr) {
      // if not contain query data, minimal sample unit is one record
      *num_global_data = text_reader.ReadAndFilterLines([this, rank, num_machines](data_size_t) {
        if (random_.NextShort(0, num_machines) == rank) {
          return true;
        } else {
          return false;
        }
      }, used_data_indices);
    } else {
      // if contain query data, minimal sample unit is one query
      data_size_t num_queries = metadata.num_queries();
      data_size_t qid = -1;
      bool is_query_used = false;
      *num_global_data = text_reader.ReadAndFilterLines(
        [this, rank, num_machines, &qid, &query_boundaries, &is_query_used, num_queries]
      (data_size_t line_idx) {
        if (qid >= num_queries) {
          Log::Fatal("Current query exceeds the range of the query file, please ensure the query file is correct");
        }
        if (line_idx >= query_boundaries[qid + 1]) {
          // if is new query
          is_query_used = false;
          if (random_.NextShort(0, num_machines) == rank) {
            is_query_used = true;
          }
          ++qid;
        }
        return is_query_used;
      }, used_data_indices);
    }
  }
  return std::move(text_reader.Lines());
}

std::vector<std::string> DatasetLoader::SampleTextDataFromMemory(const std::vector<std::string>& data) {
  int sample_cnt = io_config_.bin_construct_sample_cnt;
  if (static_cast<size_t>(sample_cnt) > data.size()) {
    sample_cnt = static_cast<int>(data.size());
  }
  auto sample_indices = random_.Sample(static_cast<int>(data.size()), sample_cnt);
  std::vector<std::string> out(sample_indices.size());
  for (size_t i = 0; i < sample_indices.size(); ++i) {
    const size_t idx = sample_indices[i];
    out[i] = data[idx];
  }
  return out;
}

std::vector<std::string> DatasetLoader::SampleTextDataFromFile(const char* filename, const Metadata& metadata, int rank, int num_machines, int* num_global_data, std::vector<data_size_t>* used_data_indices) {
  const data_size_t sample_cnt = static_cast<data_size_t>(io_config_.bin_construct_sample_cnt);
  TextReader<data_size_t> text_reader(filename, io_config_.has_header);
  std::vector<std::string> out_data;
  if (num_machines == 1 || io_config_.is_pre_partition) {
    *num_global_data = static_cast<data_size_t>(text_reader.SampleFromFile(random_, sample_cnt, &out_data));
  } else {  // need partition data
            // get query data
    const data_size_t* query_boundaries = metadata.query_boundaries();
    if (query_boundaries == nullptr) {
      // if not contain query file, minimal sample unit is one record
      *num_global_data = text_reader.SampleAndFilterFromFile([this, rank, num_machines]
      (data_size_t) {
        if (random_.NextShort(0, num_machines) == rank) {
          return true;
        } else {
          return false;
        }
      }, used_data_indices, random_, sample_cnt, &out_data);
    } else {
      // if contain query file, minimal sample unit is one query
      data_size_t num_queries = metadata.num_queries();
      data_size_t qid = -1;
      bool is_query_used = false;
      *num_global_data = text_reader.SampleAndFilterFromFile(
        [this, rank, num_machines, &qid, &query_boundaries, &is_query_used, num_queries]
      (data_size_t line_idx) {
        if (qid >= num_queries) {
          Log::Fatal("Query id exceeds the range of the query file, \
                      please ensure the query file is correct");
        }
        if (line_idx >= query_boundaries[qid + 1]) {
          // if is new query
          is_query_used = false;
          if (random_.NextShort(0, num_machines) == rank) {
            is_query_used = true;
          }
          ++qid;
        }
        return is_query_used;
      }, used_data_indices, random_, sample_cnt, &out_data);
    }
  }
  return out_data;
}

void DatasetLoader::ConstructBinMappersFromTextData(int rank, int num_machines, const std::vector<std::string>& sample_data, const Parser* parser, Dataset* dataset) {

  std::vector<std::vector<double>> sample_values;
  std::vector<std::vector<int>> sample_indices;
  std::vector<std::pair<int, double>> oneline_features;
  double label;
  for (int i = 0; i < static_cast<int>(sample_data.size()); ++i) {
    oneline_features.clear();
    // parse features
    parser->ParseOneLine(sample_data[i].c_str(), &oneline_features, &label);
    for (std::pair<int, double>& inner_data : oneline_features) {
      if (static_cast<size_t>(inner_data.first) >= sample_values.size()) {
        sample_values.resize(inner_data.first + 1);
        sample_indices.resize(inner_data.first + 1);
      }
      if (std::fabs(inner_data.second) > kEpsilon || std::isnan(inner_data.second)) {
        sample_values[inner_data.first].emplace_back(inner_data.second);
        sample_indices[inner_data.first].emplace_back(i);
      }
    }
  }

  dataset->feature_groups_.clear();

  if (feature_names_.empty()) {
    // -1 means doesn't use this feature
    dataset->used_feature_map_ = std::vector<int>(sample_values.size(), -1);
    dataset->num_total_features_ = static_cast<int>(sample_values.size());
  } else {
    dataset->used_feature_map_ = std::vector<int>(feature_names_.size(), -1);
    dataset->num_total_features_ = static_cast<int>(feature_names_.size());
  }

  // check the range of label_idx, weight_idx and group_idx
  CHECK(label_idx_ >= 0 && label_idx_ <= dataset->num_total_features_);
  CHECK(weight_idx_ < 0 || weight_idx_ < dataset->num_total_features_);
  CHECK(group_idx_ < 0 || group_idx_ < dataset->num_total_features_);

  // fill feature_names_ if not header
  if (feature_names_.empty()) {
    for (int i = 0; i < dataset->num_total_features_; ++i) {
      std::stringstream str_buf;
      str_buf << "Column_" << i;
      feature_names_.push_back(str_buf.str());
    }
  }
  dataset->set_feature_names(feature_names_);
  std::vector<std::unique_ptr<BinMapper>> bin_mappers(sample_values.size());
  const data_size_t filter_cnt = static_cast<data_size_t>(
    static_cast<double>(io_config_.min_data_in_leaf* sample_data.size()) / dataset->num_data_);

  // start find bins
  if (num_machines == 1) {
    // if only one machine, find bin locally
    OMP_INIT_EX();
    #pragma omp parallel for schedule(guided)
    for (int i = 0; i < static_cast<int>(sample_values.size()); ++i) {
      OMP_LOOP_EX_BEGIN();
      if (ignore_features_.count(i) > 0) {
        bin_mappers[i] = nullptr;
        continue;
      }
      BinType bin_type = BinType::NumericalBin;
      if (categorical_features_.count(i)) {
        bin_type = BinType::CategoricalBin;
      }
      bin_mappers[i].reset(new BinMapper());
      bin_mappers[i]->FindBin(sample_values[i].data(), static_cast<int>(sample_values[i].size()),
                              sample_data.size(), io_config_.max_bin, io_config_.min_data_in_bin, filter_cnt, bin_type, io_config_.use_missing, io_config_.zero_as_missing);
      OMP_LOOP_EX_END();
    }
    OMP_THROW_EX();
  } else {
    // if have multi-machines, need to find bin distributed
    // different machines will find bin for different features

    int total_num_feature = static_cast<int>(sample_values.size());
    total_num_feature = Network::GlobalSyncUpByMin(total_num_feature);
    // start and len will store the process feature indices for different machines
    // machine i will find bins for features in [ start[i], start[i] + len[i] )
    std::vector<int> start(num_machines);
    std::vector<int> len(num_machines);
    int step = (total_num_feature + num_machines - 1) / num_machines;
    if (step < 1) { step = 1; }

    start[0] = 0;
    for (int i = 0; i < num_machines - 1; ++i) {
      len[i] = std::min(step, total_num_feature - start[i]);
      start[i + 1] = start[i] + len[i];
    }
    len[num_machines - 1] = total_num_feature - start[num_machines - 1];
    OMP_INIT_EX();
    #pragma omp parallel for schedule(guided)
    for (int i = 0; i < len[rank]; ++i) {
      OMP_LOOP_EX_BEGIN();
      if (ignore_features_.count(start[rank] + i) > 0) {
        continue;
      }
      BinType bin_type = BinType::NumericalBin;
      if (categorical_features_.count(start[rank] + i)) {
        bin_type = BinType::CategoricalBin;
      }
      bin_mappers[i].reset(new BinMapper());
      bin_mappers[i]->FindBin(sample_values[start[rank] + i].data(), static_cast<int>(sample_values[start[rank] + i].size()),
                              sample_data.size(), io_config_.max_bin, io_config_.min_data_in_bin, filter_cnt, bin_type, io_config_.use_missing, io_config_.zero_as_missing);
      OMP_LOOP_EX_END();
    }
    OMP_THROW_EX();
    int max_bin = 0;
    for (int i = 0; i < len[rank]; ++i) {
      if (bin_mappers[i] != nullptr) {
        max_bin = std::max(max_bin, bin_mappers[i]->num_bin());
      }
    }
    max_bin = Network::GlobalSyncUpByMax(max_bin);
    // get size of bin mapper with max_bin size
    int type_size = BinMapper::SizeForSpecificBin(max_bin);
    // since sizes of different feature may not be same, we expand all bin mapper to type_size
    int buffer_size = type_size * total_num_feature;
    auto input_buffer = std::vector<char>(buffer_size);
    auto output_buffer = std::vector<char>(buffer_size);

    // find local feature bins and copy to buffer
    #pragma omp parallel for schedule(guided)
    for (int i = 0; i < len[rank]; ++i) {
      OMP_LOOP_EX_BEGIN();
      if (ignore_features_.count(start[rank] + i) > 0) {
        continue;
      }
      bin_mappers[i]->CopyTo(input_buffer.data() + i * type_size);
      // free
      bin_mappers[i].reset(nullptr);
      OMP_LOOP_EX_END();
    }
    OMP_THROW_EX();
    // convert to binary size
    for (int i = 0; i < num_machines; ++i) {
      start[i] *= type_size;
      len[i] *= type_size;
    }
    // gather global feature bin mappers
    Network::Allgather(input_buffer.data(), buffer_size, start.data(), len.data(), output_buffer.data());
    // restore features bins from buffer
    for (int i = 0; i < total_num_feature; ++i) {
      if (ignore_features_.count(i) > 0) {
        bin_mappers[i] = nullptr;
        continue;
      }
      bin_mappers[i].reset(new BinMapper());
      bin_mappers[i]->CopyFrom(output_buffer.data() + i * type_size);
    }
  }
  sample_values.clear();
  dataset->Construct(bin_mappers, Common::Vector2Ptr<int>(sample_indices).data(),
                     Common::VectorSize<int>(sample_indices).data(), sample_data.size(), io_config_);
}

/*! \brief Extract local features from memory */
void DatasetLoader::ExtractFeaturesFromMemory(std::vector<std::string>& text_data, const Parser* parser, Dataset* dataset) {
  std::vector<std::pair<int, double>> oneline_features;
  double tmp_label = 0.0f;
  if (predict_fun_ == nullptr) {
    OMP_INIT_EX();
    // if doesn't need to prediction with initial model
    #pragma omp parallel for schedule(static) private(oneline_features) firstprivate(tmp_label)
    for (data_size_t i = 0; i < dataset->num_data_; ++i) {
      OMP_LOOP_EX_BEGIN();
      const int tid = omp_get_thread_num();
      oneline_features.clear();
      // parser
      parser->ParseOneLine(text_data[i].c_str(), &oneline_features, &tmp_label);
      // set label
      dataset->metadata_.SetLabelAt(i, static_cast<float>(tmp_label));
      // free processed line:
      text_data[i].clear();
      // shrink_to_fit will be very slow in linux, and seems not free memory, disable for now
      // text_reader_->Lines()[i].shrink_to_fit();
      // push data
      for (auto& inner_data : oneline_features) {
        if (inner_data.first >= dataset->num_total_features_) { continue; }
        int feature_idx = dataset->used_feature_map_[inner_data.first];
        if (feature_idx >= 0) {
          // if is used feature
          int group = dataset->feature2group_[feature_idx];
          int sub_feature = dataset->feature2subfeature_[feature_idx];
          dataset->feature_groups_[group]->PushData(tid, sub_feature, i, inner_data.second);
        } else {
          if (inner_data.first == weight_idx_) {
            dataset->metadata_.SetWeightAt(i, static_cast<float>(inner_data.second));
          } else if (inner_data.first == group_idx_) {
            dataset->metadata_.SetQueryAt(i, static_cast<data_size_t>(inner_data.second));
          }
        }
      }
      OMP_LOOP_EX_END();
    }
    OMP_THROW_EX();
  } else {
    OMP_INIT_EX();
    // if need to prediction with initial model
    std::vector<double> init_score(dataset->num_data_ * num_class_);
    #pragma omp parallel for schedule(static) private(oneline_features) firstprivate(tmp_label)
    for (data_size_t i = 0; i < dataset->num_data_; ++i) {
      OMP_LOOP_EX_BEGIN();
      const int tid = omp_get_thread_num();
      oneline_features.clear();
      // parser
      parser->ParseOneLine(text_data[i].c_str(), &oneline_features, &tmp_label);
      // set initial score
      std::vector<double> oneline_init_score(num_class_);
      predict_fun_(oneline_features, oneline_init_score.data());
      for (int k = 0; k < num_class_; ++k) {
        init_score[k * dataset->num_data_ + i] = static_cast<double>(oneline_init_score[k]);
      }
      // set label
      dataset->metadata_.SetLabelAt(i, static_cast<float>(tmp_label));
      // free processed line:
      text_data[i].clear();
      // shrink_to_fit will be very slow in linux, and seems not free memory, disable for now
      // text_reader_->Lines()[i].shrink_to_fit();
      // push data
      for (auto& inner_data : oneline_features) {
        if (inner_data.first >= dataset->num_total_features_) { continue; }
        int feature_idx = dataset->used_feature_map_[inner_data.first];
        if (feature_idx >= 0) {
          // if is used feature
          int group = dataset->feature2group_[feature_idx];
          int sub_feature = dataset->feature2subfeature_[feature_idx];
          dataset->feature_groups_[group]->PushData(tid, sub_feature, i, inner_data.second);
        } else {
          if (inner_data.first == weight_idx_) {
            dataset->metadata_.SetWeightAt(i, static_cast<float>(inner_data.second));
          } else if (inner_data.first == group_idx_) {
            dataset->metadata_.SetQueryAt(i, static_cast<data_size_t>(inner_data.second));
          }
        }
      }
      OMP_LOOP_EX_END();
    }
    OMP_THROW_EX();
    // metadata_ will manage space of init_score
    dataset->metadata_.SetInitScore(init_score.data(), dataset->num_data_ * num_class_);
  }
  dataset->FinishLoad();
  // text data can be free after loaded feature values
  text_data.clear();
}

/*! \brief Extract local features from file */
void DatasetLoader::ExtractFeaturesFromFile(const char* filename, const Parser* parser, const std::vector<data_size_t>& used_data_indices, Dataset* dataset) {
  std::vector<double> init_score;
  if (predict_fun_ != nullptr) {
    init_score = std::vector<double>(dataset->num_data_ * num_class_);
  }
  std::function<void(data_size_t, const std::vector<std::string>&)> process_fun =
    [this, &init_score, &parser, &dataset]
  (data_size_t start_idx, const std::vector<std::string>& lines) {
    std::vector<std::pair<int, double>> oneline_features;
    double tmp_label = 0.0f;
    OMP_INIT_EX();
    #pragma omp parallel for schedule(static) private(oneline_features) firstprivate(tmp_label)
    for (data_size_t i = 0; i < static_cast<data_size_t>(lines.size()); ++i) {
      OMP_LOOP_EX_BEGIN();
      const int tid = omp_get_thread_num();
      oneline_features.clear();
      // parser
      parser->ParseOneLine(lines[i].c_str(), &oneline_features, &tmp_label);
      // set initial score
      if (!init_score.empty()) {
        std::vector<double> oneline_init_score(num_class_);
        predict_fun_(oneline_features, oneline_init_score.data());
        for (int k = 0; k < num_class_; ++k) {
          init_score[k * dataset->num_data_ + start_idx + i] = static_cast<double>(oneline_init_score[k]);
        }
      }
      // set label
      dataset->metadata_.SetLabelAt(start_idx + i, static_cast<float>(tmp_label));
      // push data
      for (auto& inner_data : oneline_features) {
        if (inner_data.first >= dataset->num_total_features_) { continue; }
        int feature_idx = dataset->used_feature_map_[inner_data.first];
        if (feature_idx >= 0) {
          // if is used feature
          int group = dataset->feature2group_[feature_idx];
          int sub_feature = dataset->feature2subfeature_[feature_idx];
          dataset->feature_groups_[group]->PushData(tid, sub_feature, start_idx + i, inner_data.second);
        } else {
          if (inner_data.first == weight_idx_) {
            dataset->metadata_.SetWeightAt(start_idx + i, static_cast<float>(inner_data.second));
          } else if (inner_data.first == group_idx_) {
            dataset->metadata_.SetQueryAt(start_idx + i, static_cast<data_size_t>(inner_data.second));
          }
        }
      }
      OMP_LOOP_EX_END();
    }
    OMP_THROW_EX();
  };
  TextReader<data_size_t> text_reader(filename, io_config_.has_header);
  if (!used_data_indices.empty()) {
    // only need part of data
    text_reader.ReadPartAndProcessParallel(used_data_indices, process_fun);
  } else {
    // need full data
    text_reader.ReadAllAndProcessParallel(process_fun);
  }

  // metadata_ will manage space of init_score
  if (!init_score.empty()) {
    dataset->metadata_.SetInitScore(init_score.data(), dataset->num_data_ * num_class_);
  }
  dataset->FinishLoad();
}

/*! \brief Check can load from binary file */
std::string DatasetLoader::CheckCanLoadFromBin(const char* filename) {
  std::string bin_filename(filename);
  bin_filename.append(".bin");

  FILE* file;

  #ifdef _MSC_VER
  fopen_s(&file, bin_filename.c_str(), "rb");
  #else
  file = fopen(bin_filename.c_str(), "rb");
  #endif

  if (file == NULL) {
    bin_filename = std::string(filename);
    #ifdef _MSC_VER
    fopen_s(&file, bin_filename.c_str(), "rb");
    #else
    file = fopen(bin_filename.c_str(), "rb");
    #endif
    if (file == NULL) {
      Log::Fatal("cannot open data file %s", bin_filename.c_str());
    }
  }

  size_t buffer_size = 256;
  auto buffer = std::vector<char>(buffer_size);
  // read size of token
  size_t size_of_token = std::strlen(Dataset::binary_file_token);
  size_t read_cnt = fread(buffer.data(), sizeof(char), size_of_token, file);
  fclose(file);
  if (read_cnt == size_of_token
      && std::string(buffer.data()) == std::string(Dataset::binary_file_token)) {
    return bin_filename;
  } else {
    return std::string();
  }

}

}
