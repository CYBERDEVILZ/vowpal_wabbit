// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/reductions/cats_tree.h"

#include "vw/core/learner.h"
#include "vw/core/simple_label.h"
#include "vw/io/logger.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace VW::LEARNER;
using std::vector;

namespace std
{
std::ostream& operator<<(std::ostream& os, const VW::reductions::cats::tree_node& node)
{
  os << "{" << node.id << "," << node.left_id << "," << node.right_id << ", " << node.parent_id << ", " << node.depth
     << ", " << (node.is_leaf ? "true" : "false") << "}";
  return os;
}
}  // namespace std

namespace VW
{
std::ostream& operator<<(std::ostream& o, VW::simple_label const& lbl)
{
  o << "{l=" << lbl.label << "}";
  return o;
}

std::ostream& operator<<(std::ostream& o, VW::simple_label_reduction_features const& red_fts)
{
  o << "{w=" << red_fts.weight << ", i=" << red_fts.initial << "}";
  return o;
}
}  // namespace VW

class reduction_test_harness
{
public:
  reduction_test_harness() : curr_idx(0) {}

  void set_predict_response(const vector<float>& preds) { predictions = preds; }

  void test_predict(base_learner& /* base */, VW::example& ec) { ec.pred.scalar = predictions[curr_idx++]; }

  void test_learn(base_learner& /* base */, VW::example& ec)
  {
    labels.emplace_back(ec.l.simple);
    weights.emplace_back(ec.weight);
    learner_offset.emplace_back(ec.ft_offset);
  }

  // use NO_SANITIZE_UNDEFINED because reference base_learner& base may be bound to nullptr
  static void NO_SANITIZE_UNDEFINED predict(reduction_test_harness& test_reduction, base_learner& base, VW::example& ec)
  {
    test_reduction.test_predict(base, ec);
  }

  static void NO_SANITIZE_UNDEFINED learn(reduction_test_harness& test_reduction, base_learner& base, VW::example& ec)
  {
    test_reduction.test_learn(base, ec);
  };

  vector<float> predictions;
  vector<VW::simple_label> labels;
  vector<float> weights;
  vector<uint64_t> learner_offset;
  int curr_idx;
};

using test_learner_t = learner<reduction_test_harness, VW::example>;
using predictions_t = vector<float>;
using scores_t = int;

template <typename T = reduction_test_harness>
learner<T, VW::example>* get_test_harness_reduction(const predictions_t& base_reduction_predictions)
{
  T* pharness = nullptr;
  return get_test_harness_reduction(base_reduction_predictions, pharness);
}

template <typename T = reduction_test_harness>
learner<T, VW::example>* get_test_harness_reduction(const predictions_t& base_reduction_predictions, T*& pharness)
{
  // Setup a test harness base reduction
  auto test_harness = VW::make_unique<T>();
  pharness = test_harness.get();
  test_harness->set_predict_response(base_reduction_predictions);
  auto test_learner = VW::LEARNER::make_base_learner(
      std::move(test_harness),          // Data structure passed by vw_framework into test_harness predict/learn calls
      reduction_test_harness::learn,    // test_harness learn
      reduction_test_harness::predict,  // test_harness predict
      "test_learner", VW::prediction_type_t::SCALAR, VW::label_type_t::CB)
                          .set_output_example_prediction([](VW::workspace& /* all */, const reduction_test_harness&,
                                                             const VW::example&, VW::io::logger&) {})

                          .build();  // Create a learner using the base reduction.
  return test_learner;
}

void predict_test_helper(const predictions_t& base_reduction_predictions, const scores_t& expected_action,
    uint32_t num_leaves, uint32_t bandwidth)
{
  const auto test_base = get_test_harness_reduction(base_reduction_predictions);
  VW::reductions::cats::cats_tree tree;
  tree.init(num_leaves, bandwidth);
  VW::example ec;
  auto ret_val = tree.predict(*as_singleline(test_base), ec);
  EXPECT_EQ(ret_val, expected_action);
  delete test_base;
}

bool operator!=(const VW::simple_label& lhs, const VW::simple_label& rhs) { return !(lhs.label == rhs.label); }

bool operator!=(const VW::simple_label_reduction_features& lhs, const VW::simple_label_reduction_features& rhs)
{
  return !(lhs.weight == rhs.weight && lhs.initial == rhs.initial);
}

TEST(cats_tree_tests, otc_algo_learn_1_action_till_root)
{
  reduction_test_harness* pharness = nullptr;
  predictions_t preds_to_return = {1.f, -1.f};
  auto* base = get_test_harness_reduction(preds_to_return, pharness);
  VW::reductions::cats::cats_tree tree;
  tree.init(4, 0);

  VW::example ec;
  ec.ft_offset = 0;
  ec.debug_current_reduction_depth = 0;
  ec.l.cb = CB::label();
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 2, 0.5f});

  tree.learn(*as_singleline(base), ec);

  // verify 1) # of calls to learn 2) passed in labels 3) passed in weights
  vector<VW::simple_label> expected_labels = {{-1}, {1}};
  vector<float> expected_weights = {3.5f / 0.5f, 3.5f / 0.5f};

  EXPECT_THAT(pharness->labels, ::testing::ContainerEq(expected_labels));
  EXPECT_THAT(pharness->weights, ::testing::ContainerEq(expected_weights));

  // verify id of learners that were trained
  vector<uint64_t> expected_learners = {1, 0};
  EXPECT_THAT(pharness->learner_offset, ::testing::ContainerEq(expected_learners));

  delete base;
}

TEST(cats_tree_tests, otc_algo_learn_1_action)
{
  reduction_test_harness* pharness = nullptr;
  predictions_t preds_to_return = {-1.f};
  auto* base = get_test_harness_reduction(preds_to_return, pharness);
  VW::reductions::cats::cats_tree tree;
  tree.init(4, 0);

  VW::example ec;
  ec.ft_offset = 0;
  ec.debug_current_reduction_depth = 0;
  ec.l.cb = CB::label();
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 2, 0.5f});

  tree.learn(*as_singleline(base), ec);

  // verify 1) # of calls to learn 2) passed in labels 3) passed in weights
  vector<VW::simple_label> expected_labels = {{-1}};
  vector<float> expected_weights = {3.5f / 0.5f};

  EXPECT_THAT(pharness->labels, ::testing::ContainerEq(expected_labels));
  EXPECT_THAT(pharness->weights, ::testing::ContainerEq(expected_weights));

  // verify id of learners that were trained
  vector<uint64_t> expected_learners = {1};
  EXPECT_THAT(pharness->learner_offset, ::testing::ContainerEq(expected_learners));

  delete base;
}

TEST(cats_tree_tests, otc_algo_learn_2_action_siblings)
{
  VW::example ec;
  ec.ft_offset = 0;
  ec.debug_current_reduction_depth = 0;
  ec.l.cb = CB::label();
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 3, 0.5f});
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 4, 0.5f});

  predictions_t preds_to_return = {1.f, -1.f};

  reduction_test_harness* pharness = nullptr;
  auto* base = get_test_harness_reduction(preds_to_return, pharness);
  VW::reductions::cats::cats_tree tree;
  tree.init(8, 0);

  tree.learn(*as_singleline(base), ec);

  // verify 1) # of calls to learn 2) passed in labels 3) passed in weights
  vector<VW::simple_label> expected_labels = {{-1}, {1}};
  vector<float> expected_weights = {3.5f / 0.5f, 3.5f / 0.5f};

  EXPECT_THAT(pharness->labels, ::testing::ContainerEq(expected_labels));
  EXPECT_THAT(pharness->weights, ::testing::ContainerEq(expected_weights));

  // verify id of learners that were trained
  vector<uint64_t> expected_learners = {1, 0};
  EXPECT_THAT(pharness->learner_offset, ::testing::ContainerEq(expected_learners));

  delete base;
}

TEST(cats_tree_tests, otc_algo_learn_2_action_notSiblings)
{
  VW::example ec;
  ec.ft_offset = 0;
  ec.debug_current_reduction_depth = 0;
  ec.l.cb = CB::label();
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 2, 0.5f});
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 3, 0.5f});

  predictions_t preds_to_return = {1.f, 1.f, -1.f, 1.f};

  reduction_test_harness* pharness = nullptr;
  auto* base = get_test_harness_reduction(preds_to_return, pharness);
  VW::reductions::cats::cats_tree tree;
  tree.init(8, 0);

  tree.learn(*as_singleline(base), ec);

  // verify 1) # of calls to learn 2) passed in labels 3) passed in weights
  vector<VW::simple_label> expected_labels = {{-1}, {1}, {1}, {1}};
  vector<float> expected_weights = {3.5f / 0.5f, 3.5f / 0.5f, 3.5f / 0.5f, 3.5f / 0.5f};

  EXPECT_THAT(pharness->labels, ::testing::ContainerEq(expected_labels));
  EXPECT_THAT(pharness->weights, ::testing::ContainerEq(expected_weights));

  // verify id of learners that were trained
  vector<uint64_t> expected_learners = {3, 4, 1, 0};
  EXPECT_THAT(pharness->learner_offset, ::testing::ContainerEq(expected_learners));

  delete base;
}

TEST(cats_tree_tests, otc_algo_learn_2_action_notSiblings_bandwidth_1)
{
  VW::example ec;
  ec.ft_offset = 0;
  ec.debug_current_reduction_depth = 0;
  ec.l.cb = CB::label();
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 2, 0.5f});
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 3, 0.5f});

  predictions_t preds_to_return = {1.f, -1.f, 1.f};

  reduction_test_harness* pharness = nullptr;
  auto* base = get_test_harness_reduction(preds_to_return, pharness);
  VW::reductions::cats::cats_tree tree;
  tree.init(8, 1);

  tree.learn(*as_singleline(base), ec);

  // verify 1) # of calls to learn 2) passed in labels 3) passed in weights
  vector<VW::simple_label> expected_labels = {
      {1},
      {1},
      {1},
  };
  vector<float> expected_weights = {3.5f / 0.5f, 3.5f / 0.5f, 3.5f / 0.5f};

  EXPECT_THAT(pharness->labels, ::testing::ContainerEq(expected_labels));
  EXPECT_THAT(pharness->weights, ::testing::ContainerEq(expected_weights));

  // verify id of learners that were trained
  vector<uint64_t> expected_learners = {4, 1, 0};
  EXPECT_THAT(pharness->learner_offset, ::testing::ContainerEq(expected_learners));

  delete base;
}

TEST(cats_tree_tests, otc_algo_learn_2_action_separate)
{
  VW::example ec;
  ec.ft_offset = 0;
  ec.debug_current_reduction_depth = 0;
  ec.l.cb = CB::label();
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 3, 0.5f});
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 6, 0.5f});

  predictions_t preds_to_return = {-1.f, -1.f, -1.f};

  reduction_test_harness* pharness = nullptr;
  auto* base = get_test_harness_reduction(preds_to_return, pharness);
  VW::reductions::cats::cats_tree tree;
  tree.init(8, 0);

  tree.learn(*as_singleline(base), ec);

  // verify 1) # of calls to learn 2) passed in labels 3) passed in weights
  vector<VW::simple_label> expected_labels = {{-1}, {1}, {-1}};
  vector<float> expected_weights = {3.5f / 0.5f, 3.5f / 0.5f, 3.5f / 0.5f};

  EXPECT_THAT(pharness->labels, ::testing::ContainerEq(expected_labels));
  EXPECT_THAT(pharness->weights, ::testing::ContainerEq(expected_weights));

  // verify id of learners that were trained
  vector<uint64_t> expected_learners = {1, 2, 0};
  EXPECT_THAT(pharness->learner_offset, ::testing::ContainerEq(expected_learners));

  delete base;
}

TEST(cats_tree_tests, otc_algo_learn_2_action_separate_2)
{
  VW::example ec;
  ec.ft_offset = 0;
  ec.debug_current_reduction_depth = 0;
  ec.l.cb = CB::label();
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 3, 0.5f});
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 7, 0.5f});

  predictions_t preds_to_return = {1.f, 1.f, 1.f, -1.f};

  reduction_test_harness* pharness = nullptr;
  auto* base = get_test_harness_reduction(preds_to_return, pharness);
  VW::reductions::cats::cats_tree tree;
  tree.init(8, 0);

  tree.learn(*as_singleline(base), ec);

  // verify 1) # of calls to learn 2) passed in labels 3) passed in weights
  vector<VW::simple_label> expected_labels = {{1}, {-1}, {1}, {1}};
  vector<float> expected_weights = {3.5f / 0.5f, 3.5f / 0.5f, 3.5f / 0.5f, 3.5f / 0.5f};

  EXPECT_THAT(pharness->labels, ::testing::ContainerEq(expected_labels));
  EXPECT_THAT(pharness->weights, ::testing::ContainerEq(expected_weights));

  // verify id of learners that were trained
  vector<uint64_t> expected_learners = {6, 1, 2, 0};
  EXPECT_THAT(pharness->learner_offset, ::testing::ContainerEq(expected_learners));

  delete base;
}

TEST(cats_tree_tests, otc_algo_learn_2_action_separate_bandwidth_2)
{
  VW::example ec;
  ec.ft_offset = 0;
  ec.debug_current_reduction_depth = 0;
  ec.l.cb = CB::label();
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 3, 0.5f});
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 6, 0.5f});

  predictions_t preds_to_return = {};

  reduction_test_harness* pharness = nullptr;
  auto* base = get_test_harness_reduction(preds_to_return, pharness);
  VW::reductions::cats::cats_tree tree;
  tree.init(8, 2);

  tree.learn(*as_singleline(base), ec);

  // verify 1) # of calls to learn 2) passed in labels 3) passed in weights
  vector<VW::simple_label> expected_labels = {};
  vector<float> expected_weights = {};

  EXPECT_THAT(pharness->labels, ::testing::ContainerEq(expected_labels));
  EXPECT_THAT(pharness->weights, ::testing::ContainerEq(expected_weights));

  // verify id of learners that were trained
  vector<uint64_t> expected_learners = {};
  EXPECT_THAT(pharness->learner_offset, ::testing::ContainerEq(expected_learners));

  delete base;
}

TEST(cats_tree_tests, otc_algo_learn_2_action_separate_2_bandwidth_2)
{
  VW::example ec;
  ec.ft_offset = 0;
  ec.debug_current_reduction_depth = 0;
  ec.l.cb = CB::label();
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 3, 0.5f});
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 11, 0.5f});

  predictions_t preds_to_return = {1, 1, -1};

  reduction_test_harness* pharness = nullptr;
  auto* base = get_test_harness_reduction(preds_to_return, pharness);
  VW::reductions::cats::cats_tree tree;
  tree.init(16, 2);

  tree.learn(*as_singleline(base), ec);

  // verify 1) # of calls to learn 2) passed in labels 3) passed in weights
  vector<VW::simple_label> expected_labels = {{1}, {1}, {1}};
  vector<float> expected_weights = {3.5f / 0.5f, 3.5f / 0.5f, 3.5f / 0.5f};

  EXPECT_THAT(pharness->labels, ::testing::ContainerEq(expected_labels));
  EXPECT_THAT(pharness->weights, ::testing::ContainerEq(expected_weights));

  // verify id of learners that were trained
  vector<uint64_t> expected_learners = {12, 5, 0};
  EXPECT_THAT(pharness->learner_offset, ::testing::ContainerEq(expected_learners));

  delete base;
}

TEST(cats_tree_tests, otc_algo_learn_2_action_separate_bandwidth_1_asym)
{
  VW::example ec;
  ec.ft_offset = 0;
  ec.debug_current_reduction_depth = 0;
  ec.l.cb = CB::label();
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 2, 0.5f});
  ec.l.cb.costs.push_back(CB::cb_class{3.5f, 5, 0.5f});

  predictions_t preds_to_return = {-1.f, 1.f, -1.f};

  reduction_test_harness* pharness = nullptr;
  auto* base = get_test_harness_reduction(preds_to_return, pharness);
  VW::reductions::cats::cats_tree tree;
  tree.init(8, 1);

  tree.learn(*as_singleline(base), ec);

  // verify 1) # of calls to learn 2) passed in labels 3) passed in weights
  vector<VW::simple_label> expected_labels = {
      {1},
      {1},
      {1},
  };
  vector<float> expected_weights = {3.5f / 0.5f, 3.5f / 0.5f, 3.5f / 0.5f};

  EXPECT_THAT(pharness->labels, ::testing::ContainerEq(expected_labels));
  EXPECT_THAT(pharness->weights, ::testing::ContainerEq(expected_weights));

  // verify id of learners that were trained
  vector<uint64_t> expected_learners = {5, 2, 0};
  EXPECT_THAT(pharness->learner_offset, ::testing::ContainerEq(expected_learners));

  delete base;
}

TEST(cats_tree_tests, offset_tree_cont_predict)
{
  // 0 node tree
  predict_test_helper({}, 0, 0, 0);
  // 2 node trees
  predict_test_helper({-1}, 1, 2, 0);
  predict_test_helper({1}, 2, 2, 0);
  // 4 node tree
  predict_test_helper({-1, 1}, 2, 4, 0);
  predict_test_helper({1, 1}, 4, 4, 0);
  // 4 node tree with bandwidth 1
  predict_test_helper({-1}, 2, 4, 1);
  predict_test_helper({1}, 3, 4, 1);
  // 8 node tree with bandwidth 1
  predict_test_helper({-1, -1}, 2, 8, 1);
  predict_test_helper({-1, 1, -1}, 3, 8, 1);
  // 8 node tree with bandwidth 2
  predict_test_helper({-1, -1}, 3, 8, 2);
  predict_test_helper({1, 1}, 6, 8, 2);
}

TEST(cats_tree_tests, build_min_depth_tree_cont_5)
{
  VW::reductions::cats::min_depth_binary_tree tree;
  tree.build_tree(4, 1);
  std::vector<VW::reductions::cats::tree_node> expected = {
      {0, 1, 2, 0, 0, false, false, false},
      {1, 3, 4, 0, 1, false, true, false},
      {2, 5, 6, 0, 1, true, false, false},
      {3, 0, 0, 1, 2, false, false, true},
      {4, 0, 0, 1, 2, false, false, true},
      {5, 0, 0, 2, 2, false, false, true},
      {6, 0, 0, 2, 2, false, false, true},
  };
  EXPECT_THAT(tree.nodes, ::testing::ContainerEq(expected));
}

TEST(cats_tree_tests, build_min_depth_tree_cont_1)
{
  VW::reductions::cats::min_depth_binary_tree tree;
  tree.build_tree(1, 0);
  std::vector<VW::reductions::cats::tree_node> expected = {{0, 0, 0, 0, 0, false, false, true}};
  EXPECT_THAT(tree.nodes, ::testing::ContainerEq(expected));
}