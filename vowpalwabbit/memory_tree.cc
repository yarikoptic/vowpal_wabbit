#include <algorithm>
#include <cmath>
#include <cstdio>
#include <float.h>
#include <time.h>
#include <sstream>
#include <ctime>

#include "reductions.h"
#include "rand48.h"
#include "vw.h"
#include "v_array.h"

using namespace std;
using namespace LEARNER;
using namespace VW::config;

namespace memory_tree_ns
{
///////////////////////Helper//////////////////////////////
//////////////////////////////////////////////////////////
template <typename T>
void remove_at_index(v_array<T>& array, uint32_t index)
{
  if (index >= array.size())
  {
    cout << "ERROR: index is larger than the size" << endl;
    return;
  }
  if (index == array.size() - 1)
  {
    array.pop();
    return;
  }
  for (size_t i = index + 1; i < array.size(); i++)
  {
    array[i - 1] = array[i];
  }
  array.pop();
  return;
}

void copy_example_data(example* dst, example* src, bool oas = false)  // copy example data.
{
  if (oas == false)
  {
    dst->l = src->l;
    dst->l.multi.label = src->l.multi.label;
  }
  else
  {
    dst->l.multilabels.label_v.delete_v();
    copy_array(dst->l.multilabels.label_v, src->l.multilabels.label_v);
  }
  VW::copy_example_data(false, dst, src);
}

inline void free_example(example* ec)
{
  VW::dealloc_example(nullptr, *ec);
  free(ec);
}

////Implement kronecker_product between two examples:
// kronecker_prod at feature level:

void diag_kronecker_prod_fs_test(
    features& f1, features& f2, features& prod_f, float& total_sum_feat_sq, float norm_sq1, float norm_sq2)
{
  prod_f.delete_v();
  if (f2.indicies.size() == 0)
    return;

  float denominator = pow(norm_sq1 * norm_sq2, 0.5f);
  size_t idx1 = 0;
  size_t idx2 = 0;

  while (idx1 < f1.size() && idx2 < f2.size())
  {
    uint64_t ec1pos = f1.indicies[idx1];
    uint64_t ec2pos = f2.indicies[idx2];

    if (ec1pos < ec2pos)
      idx1++;
    else if (ec1pos > ec2pos)
      idx2++;
    else
    {
      prod_f.push_back(f1.values[idx1] * f2.values[idx2] / denominator, ec1pos);
      total_sum_feat_sq += f1.values[idx1] * f2.values[idx2] / denominator;  // make this out of loop
      idx1++;
      idx2++;
    }
  }
}

int cmpfunc(const void* a, const void* b) { return *(char*)a - *(char*)b; }

void diag_kronecker_product_test(example& ec1, example& ec2, example& ec, bool oas = false)
{
  // copy_example_data(&ec, &ec1, oas); //no_feat false, oas: true
  VW::dealloc_example(nullptr, ec, nullptr);  // clear ec
  copy_example_data(&ec, &ec1, oas);

  ec.total_sum_feat_sq = 0.0;  // sort namespaces.  pass indices array into sort...template (leave this to the end)

  qsort(ec1.indices.begin(), ec1.indices.size(), sizeof(namespace_index), cmpfunc);
  qsort(ec2.indices.begin(), ec2.indices.size(), sizeof(namespace_index), cmpfunc);

  size_t idx1 = 0;
  size_t idx2 = 0;
  while (idx1 < ec1.indices.size() && idx2 < ec2.indices.size())
  // for (size_t idx1 = 0, idx2 = 0; idx1 < ec1.indices.size() && idx2 < ec2.indices.size(); idx1++)
  {
    namespace_index c1 = ec1.indices[idx1];
    namespace_index c2 = ec2.indices[idx2];
    if (c1 < c2)
      idx1++;
    else if (c1 > c2)
      idx2++;
    else
    {
      diag_kronecker_prod_fs_test(ec1.feature_space[c1], ec2.feature_space[c2], ec.feature_space[c1],
          ec.total_sum_feat_sq, ec1.total_sum_feat_sq, ec2.total_sum_feat_sq);
      idx1++;
      idx2++;
    }
  }
}

////////////////////////////end of helper/////////////////////////
//////////////////////////////////////////////////////////////////

////////////////////////Implementation of memory_tree///////////////////
///////////////////////////////////////////////////////////////////////

// construct node for tree.
struct node
{
  uint64_t parent;  // parent index
  int internal;
  // bool internal; //an internal or leaf
  uint32_t depth;        // depth.
  uint64_t base_router;  // use to index router.
  uint64_t left;         // left child.
  uint64_t right;        // right child.

  double nl;  // number of examples routed to left.
  double nr;  // number of examples routed to right.

  v_array<uint32_t> examples_index;

  node()  // construct:
  {
    parent = 0;
    internal = 0;  // 0:not used, 1:internal, -1:leaf
    // internal = false;
    depth = 0;
    base_router = 0;
    left = 0;
    right = 0;
    nl = 0.001;  // initilze to 1, as we need to do nl/nr.
    nr = 0.001;
    examples_index = v_init<uint32_t>();
  }
};

// memory_tree
struct memory_tree
{
  vw* all;

  v_array<node> nodes;         // array of nodes.
  v_array<example*> examples;  // array of example points

  size_t max_leaf_examples;
  size_t max_nodes;
  size_t leaf_example_multiplier;
  size_t max_routers;
  size_t max_num_labels;
  float alpha;  // for cpt type of update.
  uint64_t routers_used;
  int iter;
  uint32_t dream_repeats;  // number of dream operations per example.

  uint32_t total_num_queries;

  size_t max_depth;
  size_t max_ex_in_leaf;

  float construct_time;  // recording the time for constructing the memory tree
  float test_time;       // recording the test time

  uint32_t num_mistakes;
  bool learn_at_leaf;  // indicator for turning on learning the scorer function at the leaf level

  bool test_mode;

  size_t current_pass;  // for tracking # of passes over the dataset
  size_t final_pass;

  int top_K;  // commands:
  bool oas;   // indicator for multi-label classification (oas = 1)
  int dream_at_update;

  bool online;  // indicator for running CMT in online fashion

  float F1_score;
  float hamming_loss;

  example* kprod_ec;

  memory_tree()
  {
    nodes = v_init<node>();
    examples = v_init<example*>();
    alpha = 0.5;
    routers_used = 0;
    iter = 0;
    num_mistakes = 0;
    test_mode = false;
    max_depth = 0;
    max_ex_in_leaf = 0;
    construct_time = 0;
    test_time = 0;
    top_K = 1;
  }
};

float linear_kernel(const flat_example* fec1, const flat_example* fec2)
{
  float dotprod = 0;

  features& fs_1 = (features&)fec1->fs;
  features& fs_2 = (features&)fec2->fs;
  if (fs_2.indicies.size() == 0)
    return 0.f;

  for (size_t idx1 = 0, idx2 = 0; idx1 < fs_1.size() && idx2 < fs_2.size(); idx1++)
  {
    uint64_t ec1pos = fs_1.indicies[idx1];
    uint64_t ec2pos = fs_2.indicies[idx2];
    if (ec1pos < ec2pos)
      continue;

    while (ec1pos > ec2pos && ++idx2 < fs_2.size()) ec2pos = fs_2.indicies[idx2];

    if (ec1pos == ec2pos)
    {
      dotprod += fs_1.values[idx1] * fs_2.values[idx2];
      ++idx2;
    }
  }
  return dotprod;
}

float normalized_linear_prod(memory_tree& b, example* ec1, example* ec2)
{
  flat_example* fec1 = flatten_sort_example(*b.all, ec1);
  flat_example* fec2 = flatten_sort_example(*b.all, ec2);
  float norm_sqrt = pow(fec1->total_sum_feat_sq * fec2->total_sum_feat_sq, 0.5f);
  float linear_prod = linear_kernel(fec1, fec2);
  // fec1->fs.delete_v();
  // fec2->fs.delete_v();
  free_flatten_example(fec1);
  free_flatten_example(fec2);
  return linear_prod / norm_sqrt;
}

void init_tree(memory_tree& b)
{
  // srand48(4000);
  // simple initilization: initilize the root only
  b.iter = 0;
  b.num_mistakes = 0;
  b.routers_used = 0;
  b.test_mode = false;
  b.max_depth = 0;
  b.max_ex_in_leaf = 0;
  b.construct_time = 0;
  b.test_time = 0;
  b.top_K = 1;
  b.hamming_loss = 0.f;
  b.F1_score = 0.f;

  b.nodes.push_back(node());
  b.nodes[0].internal = -1;  // mark the root as leaf
  b.nodes[0].base_router = (b.routers_used++);

  b.kprod_ec = &calloc_or_throw<example>();  // allocate space for kronecker product example

  b.total_num_queries = 0;
  b.max_routers = b.max_nodes;
  cout << "tree initiazliation is done...." << endl
       << "max nodes " << b.max_nodes << endl
       << "tree size: " << b.nodes.size() << endl
       << "max number of unique labels: " << b.max_num_labels << endl
       << "learn at leaf: " << b.learn_at_leaf << endl
       << "num of dream operations per example: " << b.dream_repeats << endl
       << "current_pass: " << b.current_pass << endl
       << "oas: " << b.oas << endl;
}

// rout based on the prediction
inline uint64_t insert_descent(node& n, const float prediction)
{
  // prediction <0 go left, otherwise go right
  if (prediction < 0)
  {
    n.nl++;  // increment the number of examples routed to the left.
    return n.left;
  }
  else
  {          // otherwise go right.
    n.nr++;  // increment the number of examples routed to the right.
    return n.right;
  }
}

// return the id of the example and the leaf id (stored in cn)
inline int random_sample_example_pop(memory_tree& b, uint64_t& cn)
{
  cn = 0;  // always start from the root:
  while (b.nodes[cn].internal == 1)
  {
    float pred = 0.;              // deal with some edge cases:
    if (b.nodes[cn].nl < 1)       // no examples routed to left ever:
      pred = 1.f;                 // go right.
    else if (b.nodes[cn].nr < 1)  // no examples routed to right ever:
      pred = -1.f;                // go left.
    else if ((b.nodes[cn].nl >= 1) && (b.nodes[cn].nr >= 1))
      pred = merand48(b.all->random_state) < (b.nodes[cn].nl * 1. / (b.nodes[cn].nr + b.nodes[cn].nl)) ? -1.f : 1.f;
    else
    {
      cout << cn << " " << b.nodes[cn].nl << " " << b.nodes[cn].nr << endl;
      cout << "Error:  nl = 0, and nr = 0, exit...";
      exit(0);
    }

    if (pred < 0)
    {
      b.nodes[cn].nl--;
      cn = b.nodes[cn].left;
    }
    else
    {
      b.nodes[cn].nr--;
      cn = b.nodes[cn].right;
    }
  }

  if (b.nodes[cn].examples_index.size() >= 1)
  {
    int loc_at_leaf = int(merand48(b.all->random_state) * b.nodes[cn].examples_index.size());
    uint32_t ec_id = b.nodes[cn].examples_index[loc_at_leaf];
    remove_at_index(b.nodes[cn].examples_index, loc_at_leaf);
    return ec_id;
  }
  else
    return -1;
}

// train the node with id cn, using the statistics stored in the node to
// formulate a binary classificaiton example.
float train_node(memory_tree& b, single_learner& base, example& ec, const uint64_t cn)
{
  // predict, learn and predict
  // note: here we first train the router and then predict.
  MULTICLASS::label_t mc;
  uint32_t save_multi_pred = 0;
  MULTILABEL::labels multilabels;
  MULTILABEL::labels preds;
  if (b.oas == false)
  {
    mc = ec.l.multi;
    save_multi_pred = ec.pred.multiclass;
  }
  else
  {
    multilabels = ec.l.multilabels;
    preds = ec.pred.multilabels;
  }

  ec.l.simple = {1.f, 1.f, 0.};
  base.predict(ec, b.nodes[cn].base_router);
  float prediction = ec.pred.scalar;
  // float imp_weight = 1.f; //no importance weight.

  float weighted_value =
      (float)((1. - b.alpha) * log(b.nodes[cn].nl / (b.nodes[cn].nr + 1e-1)) / log(2.) + b.alpha * prediction);
  float route_label = weighted_value < 0.f ? -1.f : 1.f;

  // ec.l.simple = {route_label, imp_weight, 0.f};
  float ec_input_weight = ec.weight;
  ec.weight = 1.f;
  ec.l.simple = {route_label, 1., 0.f};
  base.learn(ec, b.nodes[cn].base_router);  // update the router according to the new example.

  base.predict(ec, b.nodes[cn].base_router);
  float save_binary_scalar = ec.pred.scalar;

  if (b.oas == false)
  {
    ec.l.multi = mc;
    ec.pred.multiclass = save_multi_pred;
  }
  else
  {
    ec.pred.multilabels = preds;
    ec.l.multilabels = multilabels;
  }
  ec.weight = ec_input_weight;

  return save_binary_scalar;
}

// turn a leaf into an internal node, and create two children
// when the number of examples is too big
void split_leaf(memory_tree& b, single_learner& base, const uint64_t cn)
{
  // create two children
  b.nodes[cn].internal = 1;  // swith to internal node.
  uint32_t left_child = (uint32_t)b.nodes.size();
  b.nodes.push_back(node());
  b.nodes[left_child].internal = -1;  // left leaf
  b.nodes[left_child].base_router = (b.routers_used++);
  uint32_t right_child = (uint32_t)b.nodes.size();
  b.nodes.push_back(node());
  b.nodes[right_child].internal = -1;  // right leaf
  b.nodes[right_child].base_router = (b.routers_used++);

  if (b.nodes[cn].depth + 1 > b.max_depth)
  {
    b.max_depth = b.nodes[cn].depth + 1;
    cout << "depth " << b.max_depth << endl;
  }

  b.nodes[cn].left = left_child;
  b.nodes[cn].right = right_child;
  b.nodes[left_child].parent = cn;
  b.nodes[right_child].parent = cn;
  b.nodes[left_child].depth = b.nodes[cn].depth + 1;
  b.nodes[right_child].depth = b.nodes[cn].depth + 1;

  if (b.nodes[left_child].depth > b.max_depth)
    b.max_depth = b.nodes[left_child].depth;

  // rout the examples stored in the node to the left and right
  for (size_t ec_id = 0; ec_id < b.nodes[cn].examples_index.size(); ec_id++)  // scan all examples stored in the cn
  {
    uint32_t ec_pos = b.nodes[cn].examples_index[ec_id];
    MULTICLASS::label_t mc;
    uint32_t save_multi_pred = 0;
    MULTILABEL::labels multilabels;
    MULTILABEL::labels preds;
    if (b.oas == false)
    {
      mc = b.examples[ec_pos]->l.multi;
      save_multi_pred = b.examples[ec_pos]->pred.multiclass;
    }
    else
    {
      multilabels = b.examples[ec_pos]->l.multilabels;
      preds = b.examples[ec_pos]->pred.multilabels;
    }

    b.examples[ec_pos]->l.simple = {1.f, 1.f, 0.f};
    base.predict(*b.examples[ec_pos], b.nodes[cn].base_router);  // re-predict
    float scalar = b.examples[ec_pos]->pred.scalar;              // this is spliting the leaf.
    if (scalar < 0)
    {
      b.nodes[left_child].examples_index.push_back(ec_pos);
      float leaf_pred = train_node(b, base, *b.examples[ec_pos], left_child);
      insert_descent(b.nodes[left_child], leaf_pred);  // fake descent, only for update nl and nr
    }
    else
    {
      b.nodes[right_child].examples_index.push_back(ec_pos);
      float leaf_pred = train_node(b, base, *b.examples[ec_pos], right_child);
      insert_descent(b.nodes[right_child], leaf_pred);  // fake descent. for update nr and nl
    }

    if (b.oas == false)
    {
      b.examples[ec_pos]->l.multi = mc;
      b.examples[ec_pos]->pred.multiclass = save_multi_pred;
    }
    else
    {
      b.examples[ec_pos]->pred.multilabels = preds;
      b.examples[ec_pos]->l.multilabels = multilabels;
    }
  }
  b.nodes[cn].examples_index.delete_v();                                                  // empty the cn's example list
  b.nodes[cn].nl = (std::max)(double(b.nodes[left_child].examples_index.size()), 0.001);  // avoid to set nl to zero
  b.nodes[cn].nr = (std::max)(double(b.nodes[right_child].examples_index.size()), 0.001);  // avoid to set nr to zero

  if ((std::max)(b.nodes[cn].nl, b.nodes[cn].nr) > b.max_ex_in_leaf)
  {
    b.max_ex_in_leaf = (size_t)(std::max)(b.nodes[cn].nl, b.nodes[cn].nr);
    // cout<<b.max_ex_in_leaf<<endl;
  }
}

int compare_label(const void* a, const void* b) { return *(uint32_t*)a - *(uint32_t*)b; }

inline uint32_t over_lap(v_array<uint32_t>& array_1, v_array<uint32_t>& array_2)
{
  uint32_t num_overlap = 0;

  qsort(array_1.begin(), array_1.size(), sizeof(uint32_t), compare_label);
  qsort(array_2.begin(), array_2.size(), sizeof(uint32_t), compare_label);

  uint32_t idx1 = 0;
  uint32_t idx2 = 0;
  while (idx1 < array_1.size() && idx2 < array_2.size())
  {
    uint32_t c1 = array_1[idx1];
    uint32_t c2 = array_2[idx2];
    if (c1 < c2)
      idx1++;
    else if (c1 > c2)
      idx2++;
    else
    {
      num_overlap++;
      idx1++;
      idx2++;
    }
  }
  return num_overlap;
}

// template<typename T>
inline uint32_t hamming_loss(v_array<uint32_t>& array_1, v_array<uint32_t>& array_2)
{
  uint32_t overlap = over_lap(array_1, array_2);
  return (uint32_t)(array_1.size() + array_2.size() - 2 * overlap);
}

void collect_labels_from_leaf(memory_tree& b, const uint64_t cn, v_array<uint32_t>& leaf_labs)
{
  if (b.nodes[cn].internal != -1)
    cout << "something is wrong, it should be a leaf node" << endl;

  leaf_labs.clear();
  for (size_t i = 0; i < b.nodes[cn].examples_index.size(); i++)
  {  // scan through each memory in the leaf
    uint32_t loc = b.nodes[cn].examples_index[i];
    for (uint32_t lab : b.examples[loc]->l.multilabels.label_v)
    {  // scan through each label:
      if (v_array_contains(leaf_labs, lab) == false)
        leaf_labs.push_back(lab);
    }
  }
}

inline void train_one_against_some_at_leaf(memory_tree& b, single_learner& base, const uint64_t cn, example& ec)
{
  v_array<uint32_t> leaf_labs = v_init<uint32_t>();
  collect_labels_from_leaf(b, cn, leaf_labs);  // unique labels from the leaf.
  MULTILABEL::labels multilabels = ec.l.multilabels;
  MULTILABEL::labels preds = ec.pred.multilabels;
  ec.l.simple = {FLT_MAX, 1.f, 0.f};
  for (size_t i = 0; i < leaf_labs.size(); i++)
  {
    ec.l.simple.label = -1.f;
    if (v_array_contains(multilabels.label_v, leaf_labs[i]))
      ec.l.simple.label = 1.f;
    base.learn(ec, b.max_routers + 1 + leaf_labs[i]);
  }
  ec.pred.multilabels = preds;
  ec.l.multilabels = multilabels;
}

inline uint32_t compute_hamming_loss_via_oas(
    memory_tree& b, single_learner& base, const uint64_t cn, example& ec, v_array<uint32_t>& selected_labs)
{
  selected_labs.delete_v();
  v_array<uint32_t> leaf_labs = v_init<uint32_t>();
  collect_labels_from_leaf(b, cn, leaf_labs);  // unique labels stored in the leaf.
  MULTILABEL::labels multilabels = ec.l.multilabels;
  MULTILABEL::labels preds = ec.pred.multilabels;
  ec.l.simple = {FLT_MAX, 1.f, 0.f};
  for (size_t i = 0; i < leaf_labs.size(); i++)
  {
    base.predict(ec, b.max_routers + 1 + leaf_labs[i]);
    float score = ec.pred.scalar;
    if (score > 0)
      selected_labs.push_back(leaf_labs[i]);
  }
  ec.pred.multilabels = preds;
  ec.l.multilabels = multilabels;

  return hamming_loss(ec.l.multilabels.label_v, selected_labs);
}

// pick up the "closest" example in the leaf using the score function.
int64_t pick_nearest(memory_tree& b, single_learner& base, const uint64_t cn, example& ec)
{
  if (b.nodes[cn].examples_index.size() > 0)
  {
    float max_score = -FLT_MAX;
    int64_t max_pos = -1;
    for (size_t i = 0; i < b.nodes[cn].examples_index.size(); i++)
    {
      float score = 0.f;
      uint32_t loc = b.nodes[cn].examples_index[i];

      // do not use reward to update memory tree during the very first pass
      //(which is for unsupervised training for memory tree)
      if (b.learn_at_leaf == true && b.current_pass >= 1)
      {
        float tmp_s = normalized_linear_prod(b, &ec, b.examples[loc]);
        diag_kronecker_product_test(ec, *b.examples[loc], *b.kprod_ec, b.oas);
        b.kprod_ec->l.simple = {FLT_MAX, 0., tmp_s};
        base.predict(*b.kprod_ec, b.max_routers);
        score = b.kprod_ec->partial_prediction;
      }
      else
        score = normalized_linear_prod(b, &ec, b.examples[loc]);

      if (score > max_score)
      {
        max_score = score;
        max_pos = (int64_t)loc;
      }
    }
    return max_pos;
  }
  else
    return -1;
}

// for any two examples, use number of overlap labels to indicate the similarity between these two examples.
float get_overlap_from_two_examples(example& ec1, example& ec2)
{
  return (float)over_lap(ec1.l.multilabels.label_v, ec2.l.multilabels.label_v);
}

// we use F1 score as the reward signal
float F1_score_for_two_examples(example& ec1, example& ec2)
{
  float num_overlaps = get_overlap_from_two_examples(ec1, ec2);
  float v1 = (float)(num_overlaps / (1e-7 + ec1.l.multilabels.label_v.size() * 1.));
  float v2 = (float)(num_overlaps / (1e-7 + ec2.l.multilabels.label_v.size() * 1.));
  if (num_overlaps == 0.f)
    return 0.f;
  else
    // return v2; //only precision
    return 2.f * (v1 * v2 / (v1 + v2));
}

void predict(memory_tree& b, single_learner& base, example& ec)
{
  MULTICLASS::label_t mc;
  uint32_t save_multi_pred = 0;
  MULTILABEL::labels multilabels;
  MULTILABEL::labels preds;
  if (b.oas == false)
  {
    mc = ec.l.multi;
    save_multi_pred = ec.pred.multiclass;
  }
  else
  {
    multilabels = ec.l.multilabels;
    preds = ec.pred.multilabels;
  }

  uint64_t cn = 0;
  ec.l.simple = {-1.f, 1.f, 0.};
  while (b.nodes[cn].internal == 1)
  {  // if it's internal{
    base.predict(ec, b.nodes[cn].base_router);
    uint64_t newcn = ec.pred.scalar < 0 ? b.nodes[cn].left : b.nodes[cn].right;  // do not need to increment nl and nr.
    cn = newcn;
  }

  if (b.oas == false)
  {
    ec.l.multi = mc;
    ec.pred.multiclass = save_multi_pred;
  }
  else
  {
    ec.pred.multilabels = preds;
    ec.l.multilabels = multilabels;
  }

  int64_t closest_ec = 0;
  if (b.oas == false)
  {
    closest_ec = pick_nearest(b, base, cn, ec);
    if (closest_ec != -1)
      ec.pred.multiclass = b.examples[closest_ec]->l.multi.label;
    else
      ec.pred.multiclass = 0;

    if (ec.l.multi.label != ec.pred.multiclass)
    {
      ec.loss = ec.weight;
      b.num_mistakes++;
    }
  }
  else
  {
    float reward = 0.f;
    closest_ec = pick_nearest(b, base, cn, ec);
    if (closest_ec != -1)
    {
      reward = F1_score_for_two_examples(ec, *b.examples[closest_ec]);
      b.F1_score += reward;
    }
    v_array<uint32_t> selected_labs = v_init<uint32_t>();
    ec.loss = (float)compute_hamming_loss_via_oas(b, base, cn, ec, selected_labs);
    b.hamming_loss += ec.loss;
  }
}

float return_reward_from_node(memory_tree& b, single_learner& base, uint64_t cn, example& ec, float weight = 1.f)
{
  // example& ec = *b.examples[ec_array_index];
  MULTICLASS::label_t mc;
  uint32_t save_multi_pred = 0;
  MULTILABEL::labels multilabels;
  MULTILABEL::labels preds;
  if (b.oas == false)
  {
    mc = ec.l.multi;
    save_multi_pred = ec.pred.multiclass;
  }
  else
  {
    multilabels = ec.l.multilabels;
    preds = ec.pred.multilabels;
  }
  ec.l.simple = {FLT_MAX, 1., 0.0};
  while (b.nodes[cn].internal != -1)
  {
    base.predict(ec, b.nodes[cn].base_router);
    float prediction = ec.pred.scalar;
    cn = prediction < 0 ? b.nodes[cn].left : b.nodes[cn].right;
  }

  if (b.oas == false)
  {
    ec.l.multi = mc;
    ec.pred.multiclass = save_multi_pred;
  }
  else
  {
    ec.pred.multilabels = preds;
    ec.l.multilabels = multilabels;
  }

  // get to leaf now:
  int64_t closest_ec = 0;
  float reward = 0.f;
  closest_ec = pick_nearest(b, base, cn, ec);  // no randomness for picking example.
  if (b.oas == false)
  {
    if ((closest_ec != -1) && (b.examples[closest_ec]->l.multi.label == ec.l.multi.label))
      reward = 1.f;
  }
  else
  {
    if (closest_ec != -1)
      reward = F1_score_for_two_examples(ec, *b.examples[closest_ec]);
  }
  b.total_num_queries++;

  if (b.learn_at_leaf == true && closest_ec != -1)
  {
    float score = normalized_linear_prod(b, &ec, b.examples[closest_ec]);
    diag_kronecker_product_test(ec, *b.examples[closest_ec], *b.kprod_ec, b.oas);
    b.kprod_ec->l.simple = {reward, 1.f, -score};
    b.kprod_ec->weight = weight;
    base.learn(*b.kprod_ec, b.max_routers);
  }

  if (b.oas == true)
    train_one_against_some_at_leaf(b, base, cn, ec);  /// learn the inference procedure anyway

  return reward;
}

void learn_at_leaf_random(
    memory_tree& b, single_learner& base, const uint64_t& leaf_id, example& ec, const float& weight)
{
  b.total_num_queries++;
  int32_t ec_id = -1;
  float reward = 0.f;
  if (b.nodes[leaf_id].examples_index.size() > 0)
  {
    uint32_t pos = uint32_t(merand48(b.all->random_state) * b.nodes[leaf_id].examples_index.size());
    ec_id = b.nodes[leaf_id].examples_index[pos];
  }
  if (ec_id != -1)
  {
    if (b.examples[ec_id]->l.multi.label == ec.l.multi.label)
      reward = 1.f;
    float score = normalized_linear_prod(b, &ec, b.examples[ec_id]);
    diag_kronecker_product_test(ec, *b.examples[ec_id], *b.kprod_ec, b.oas);
    b.kprod_ec->l.simple = {reward, 1.f, -score};
    b.kprod_ec->weight = weight;  //* b.nodes[leaf_id].examples_index.size();
    base.learn(*b.kprod_ec, b.max_routers);
  }
  return;
}

void route_to_leaf(memory_tree& b, single_learner& base, const uint32_t& ec_array_index, uint64_t cn,
    v_array<uint64_t>& path, bool insertion)
{
  example& ec = *b.examples[ec_array_index];

  MULTICLASS::label_t mc;
  uint32_t save_multi_pred = 0;
  MULTILABEL::labels multilabels;
  MULTILABEL::labels preds;
  if (b.oas == false)
  {
    mc = ec.l.multi;
    save_multi_pred = ec.pred.multiclass;
  }
  else
  {
    multilabels = ec.l.multilabels;
    preds = ec.pred.multilabels;
  }

  path.clear();
  ec.l.simple = {FLT_MAX, 1.0, 0.0};
  while (b.nodes[cn].internal != -1)
  {
    path.push_back(cn);  // path stores node id from the root to the leaf
    base.predict(ec, b.nodes[cn].base_router);
    float prediction = ec.pred.scalar;
    if (insertion == false)
      cn = prediction < 0 ? b.nodes[cn].left : b.nodes[cn].right;
    else
      cn = insert_descent(b.nodes[cn], prediction);
  }
  path.push_back(cn);  // push back the leaf

  if (b.oas == false)
  {
    ec.l.multi = mc;
    ec.pred.multiclass = save_multi_pred;
  }
  else
  {
    ec.pred.multilabels = preds;
    ec.l.multilabels = multilabels;
  }

  // cout<<"at route to leaf: "<<path.size()<<endl;
  if (insertion == true)
  {
    b.nodes[cn].examples_index.push_back(ec_array_index);
    if ((b.nodes[cn].examples_index.size() >= b.max_leaf_examples) && (b.nodes.size() + 2 < b.max_nodes))
      split_leaf(b, base, cn);
  }
}

// we roll in, then stop at a random step, do exploration. //no real insertion happens in the function.
void single_query_and_learn(memory_tree& b, single_learner& base, const uint32_t& ec_array_index, example& ec)
{
  v_array<uint64_t> path_to_leaf = v_init<uint64_t>();
  route_to_leaf(b, base, ec_array_index, 0, path_to_leaf, false);  // no insertion happens here.

  if (path_to_leaf.size() > 1)
  {
    // uint32_t random_pos = merand48(b.all->random_state)*(path_to_leaf.size()-1);
    uint32_t random_pos = (uint32_t)(merand48(b.all->random_state) * (path_to_leaf.size()));  // include leaf
    uint64_t cn = path_to_leaf[random_pos];

    if (b.nodes[cn].internal != -1)
    {  // if it's an internal node:'
      float objective = 0.f;
      float prob_right = 0.5;
      float coin = merand48(b.all->random_state) < prob_right ? 1.f : -1.f;
      float weight = path_to_leaf.size() * 1.f / (path_to_leaf.size() - 1.f);
      if (coin == -1.f)
      {  // go left
        float reward_left_subtree = return_reward_from_node(b, base, b.nodes[cn].left, ec, weight);
        objective = (float)((1. - b.alpha) * log(b.nodes[cn].nl / b.nodes[cn].nr) +
            b.alpha * (-reward_left_subtree / (1. - prob_right)) / 2.);
      }
      else
      {  // go right:
        float reward_right_subtree = return_reward_from_node(b, base, b.nodes[cn].right, ec, weight);
        objective = (float)((1. - b.alpha) * log(b.nodes[cn].nl / b.nodes[cn].nr) +
            b.alpha * (reward_right_subtree / prob_right) / 2.);
      }

      float ec_input_weight = ec.weight;

      MULTICLASS::label_t mc;
      MULTILABEL::labels multilabels;
      MULTILABEL::labels preds;
      if (b.oas == false)
        mc = ec.l.multi;
      else
      {
        multilabels = ec.l.multilabels;
        preds = ec.pred.multilabels;
      }

      ec.weight = fabs(objective);
      if (ec.weight >= 100.f)  // crop the weight, otherwise sometimes cause NAN outputs.
        ec.weight = 100.f;
      else if (ec.weight < .01f)
        ec.weight = 0.01f;
      ec.l.simple = {objective < 0. ? -1.f : 1.f, 1.f, 0.};
      base.learn(ec, b.nodes[cn].base_router);

      if (b.oas == false)
        ec.l.multi = mc;
      else
      {
        ec.pred.multilabels = preds;
        ec.l.multilabels = multilabels;
      }
      ec.weight = ec_input_weight;  // restore the original weight
    }
    else
    {                      // if it's a leaf node:
      float weight = 1.f;  // float(path_to_leaf.size());
      if (b.learn_at_leaf == true)
        learn_at_leaf_random(
            b, base, cn, ec, weight);  // randomly sample one example, query reward, and update leaf learner

      if (b.oas == true)
        train_one_against_some_at_leaf(b, base, cn, ec);
    }
  }
  path_to_leaf.delete_v();
}

// using reward signals
void update_rew(memory_tree& b, single_learner& base, const uint32_t& ec_array_index, example& ec)
{
  single_query_and_learn(b, base, ec_array_index, ec);
}

// node here the ec is already stored in the b.examples, the task here is to rout it to the leaf,
// and insert the ec_array_index to the leaf.
void insert_example(memory_tree& b, single_learner& base, const uint32_t& ec_array_index, bool fake_insert = false)
{
  uint64_t cn = 0;                   // start from the root.
  while (b.nodes[cn].internal == 1)  // if it's internal node:
  {
    // predict and train the node at cn.
    float router_pred = train_node(b, base, *b.examples[ec_array_index], cn);
    uint64_t newcn = insert_descent(b.nodes[cn], router_pred);  // updated nr or nl
    cn = newcn;
  }

  if (b.oas == true)  // if useing oas as inference procedure, we just train oas here, as it's independent of the memory
                      // unit anyway'
    train_one_against_some_at_leaf(b, base, cn, *b.examples[ec_array_index]);

  if ((b.nodes[cn].internal == -1) && (fake_insert == false))  // get to leaf:
  {
    b.nodes[cn].examples_index.push_back(ec_array_index);
    if (b.nodes[cn].examples_index.size() > b.max_ex_in_leaf)
    {
      b.max_ex_in_leaf = b.nodes[cn].examples_index.size();
    }
    float leaf_pred = train_node(b, base, *b.examples[ec_array_index], cn);  // tain the leaf as well.
    insert_descent(b.nodes[cn], leaf_pred);  // this is a faked descent, the purpose is only to update nl and nr of cn

    // if the number of examples exceeds the max_leaf_examples, and not reach the max_nodes - 2 yet, we split:
    if ((b.nodes[cn].examples_index.size() >= b.max_leaf_examples) && (b.nodes.size() + 2 <= b.max_nodes))
    {
      split_leaf(b, base, cn);
    }
  }
}

void experience_replay(memory_tree& b, single_learner& base)
{
  uint64_t cn = 0;  // start from root, randomly descent down!
  int ec_id = random_sample_example_pop(b, cn);
  if (ec_id >= 0)
  {
    if (b.current_pass < 1)
      insert_example(b, base, ec_id);  // unsupervised learning
    else
    {
      if (b.dream_at_update == false)
      {
        v_array<uint64_t> tmp_path = v_init<uint64_t>();
        route_to_leaf(b, base, ec_id, 0, tmp_path, true);
        tmp_path.delete_v();
      }
      else
      {
        insert_example(b, base, ec_id);
      }
    }
  }
}

// learn: descent the example from the root while generating binary training
// example for each node, including the leaf, and store the example at the leaf.
void learn(memory_tree& b, single_learner& base, example& ec)
{
  if (b.test_mode == false)
  {
    b.iter++;
    predict(b, base, ec);

    if (b.iter % 5000 == 0)
    {
      if (b.oas == false)
        cout << "at iter " << b.iter << ", top(" << b.top_K << ") pred error: " << b.num_mistakes * 1. / b.iter
             << ", total num queires so far: " << b.total_num_queries << ", max depth: " << b.max_depth
             << ", max exp in leaf: " << b.max_ex_in_leaf << endl;
      else
        cout << "at iter " << b.iter << ", avg hamming loss: " << b.hamming_loss * 1. / b.iter << endl;
    }

    clock_t begin = clock();

    if (b.current_pass < 1)
    {  // in the first pass, we need to store the memory:
      example* new_ec = &calloc_or_throw<example>();
      copy_example_data(new_ec, &ec, b.oas);
      b.examples.push_back(new_ec);
      if (b.online == true)
        update_rew(b, base, (uint32_t)(b.examples.size() - 1), *b.examples[b.examples.size() - 1]);  // query and learn

      insert_example(b, base, (uint32_t)(b.examples.size() - 1));  // unsupervised learning.
      for (uint32_t i = 0; i < b.dream_repeats; i++) experience_replay(b, base);
    }
    else
    {  // starting from the current pass, we just learn using reinforcement signal, no insertion needed:
      size_t ec_id = (b.iter) % b.examples.size();
      update_rew(b, base, (uint32_t)ec_id, *b.examples[ec_id]);  // no insertion will happen in this call
      for (uint32_t i = 0; i < b.dream_repeats; i++) experience_replay(b, base);
    }
    b.construct_time += float(clock() - begin) / CLOCKS_PER_SEC;
  }
  else if (b.test_mode == true)
  {
    b.iter++;
    if (b.iter % 5000 == 0)
    {
      if (b.oas == false)
        cout << "at iter " << b.iter << ", pred error: " << b.num_mistakes * 1. / b.iter << endl;
      else
        cout << "at iter " << b.iter << ", avg hamming loss: " << b.hamming_loss * 1. / b.iter << endl;
    }
    clock_t begin = clock();
    predict(b, base, ec);
    b.test_time += float(clock() - begin) / CLOCKS_PER_SEC;
  }
}

void end_pass(memory_tree& b)
{
  b.current_pass++;
  cout << "######### Current Pass: " << b.current_pass
       << ", with number of memories strored so far: " << b.examples.size() << endl;
}

void finish(memory_tree& b)
{
  for (size_t i = 0; i < b.nodes.size(); ++i) b.nodes[i].examples_index.delete_v();
  b.nodes.delete_v();
  for (size_t i = 0; i < b.examples.size(); i++) free_example(b.examples[i]);
  b.examples.delete_v();
  free_example(b.kprod_ec);
  // cout<<b.max_nodes<<endl;
  // cout<<b.construct_time<<" "<<b.test_time<<endl;
}

///////////////////Save & Load//////////////////////////////////////
////////////////////////////////////////////////////////////////////

void save_load_example(example* ec, io_buf& model_file, bool& read, bool& text, stringstream& msg, bool& oas)
{  // deal with tag
   // deal with labels:
  writeit(ec->num_features, "num_features");
  writeit(ec->total_sum_feat_sq, "total_sum_features");
  writeit(ec->weight, "example_weight");
  writeit(ec->loss, "loss");
  writeit(ec->ft_offset, "ft_offset");
  if (oas == false)
  {  // multi-class
    writeit(ec->l.multi.label, "multiclass_label");
    writeit(ec->l.multi.weight, "multiclass_weight");
  }
  else
  {  // multi-label
    writeitvar(ec->l.multilabels.label_v.size(), "label_size", label_size);
    if (read)
    {
      ec->l.multilabels.label_v.clear();
      for (uint32_t i = 0; i < label_size; i++) ec->l.multilabels.label_v.push_back(0);
    }
    for (uint32_t i = 0; i < label_size; i++) writeit(ec->l.multilabels.label_v[i], "ec_label");
  }

  writeitvar(ec->tag.size(), "tags", tag_number);
  if (read)
  {
    ec->tag.clear();
    for (uint32_t i = 0; i < tag_number; i++) ec->tag.push_back('a');
  }
  for (uint32_t i = 0; i < tag_number; i++) writeit(ec->tag[i], "tag");

  // deal with tag:
  writeitvar(ec->indices.size(), "namespaces", namespace_size);
  if (read)
  {
    ec->indices.delete_v();
    for (uint32_t i = 0; i < namespace_size; i++)
    {
      ec->indices.push_back('\0');
    }
  }
  for (uint32_t i = 0; i < namespace_size; i++) writeit(ec->indices[i], "namespace_index");

  // deal with features
  for (namespace_index nc : ec->indices)
  {
    features* fs = &ec->feature_space[nc];
    writeitvar(fs->size(), "features_", feat_size);
    if (read)
    {
      fs->clear();
      fs->values = v_init<feature_value>();
      fs->indicies = v_init<feature_index>();
      for (uint32_t f_i = 0; f_i < feat_size; f_i++)
      {
        fs->push_back(0, 0);
      }
    }
    for (uint32_t f_i = 0; f_i < feat_size; f_i++) writeit(fs->values[f_i], "value");
    for (uint32_t f_i = 0; f_i < feat_size; f_i++) writeit(fs->indicies[f_i], "index");
  }
}

void save_load_node(node& cn, io_buf& model_file, bool& read, bool& text, stringstream& msg)
{
  writeit(cn.parent, "parent");
  writeit(cn.internal, "internal");
  writeit(cn.depth, "depth");
  writeit(cn.base_router, "base_router");
  writeit(cn.left, "left");
  writeit(cn.right, "right");
  writeit(cn.nl, "nl");
  writeit(cn.nr, "nr");
  writeitvar(cn.examples_index.size(), "leaf_n_examples", leaf_n_examples);
  if (read)
  {
    cn.examples_index.clear();
    for (uint32_t k = 0; k < leaf_n_examples; k++) cn.examples_index.push_back(0);
  }
  for (uint32_t k = 0; k < leaf_n_examples; k++) writeit(cn.examples_index[k], "example_location");
}

void save_load_memory_tree(memory_tree& b, io_buf& model_file, bool read, bool text)
{
  stringstream msg;
  if (model_file.files.size() > 0)
  {
    if (read)
      b.test_mode = true;

    if (read)
    {
      uint32_t ss = 0;
      writeit(ss, "stride_shift");
      b.all->weights.stride_shift(ss);
    }
    else
    {
      size_t ss = b.all->weights.stride_shift();
      writeit(ss, "stride_shift");
    }

    writeit(b.max_nodes, "max_nodes");
    writeit(b.learn_at_leaf, "learn_at_leaf");
    writeit(b.oas, "oas");
    // writeit(b.leaf_example_multiplier, "leaf_example_multiplier")
    writeitvar(b.nodes.size(), "nodes", n_nodes);
    writeit(b.max_num_labels, "max_number_of_labels");

    if (read)
    {
      b.nodes.clear();
      for (uint32_t i = 0; i < n_nodes; i++) b.nodes.push_back(node());
    }

    // node
    for (uint32_t i = 0; i < n_nodes; i++)
    {
      save_load_node(b.nodes[i], model_file, read, text, msg);
    }
    // deal with examples:
    writeitvar(b.examples.size(), "examples", n_examples);
    if (read)
    {
      b.examples.clear();
      for (uint32_t i = 0; i < n_examples; i++)
      {
        example* new_ec = &calloc_or_throw<example>();
        b.examples.push_back(new_ec);
      }
    }
    for (uint32_t i = 0; i < n_examples; i++) save_load_example(b.examples[i], model_file, read, text, msg, b.oas);
    // cout<<"done loading...."<<endl;
  }
}
//////////////////////////////End of Save & Load///////////////////////////////
}  // namespace memory_tree_ns

base_learner* memory_tree_setup(options_i& options, vw& all)
{
  using namespace memory_tree_ns;
  auto tree = scoped_calloc_or_throw<memory_tree>();
  option_group_definition new_options("Memory Tree");

  new_options
      .add(make_option("memory_tree", tree->max_nodes)
               .keep()
               .default_value(0)
               .help("Make a memory tree with at most <n> nodes"))
      .add(make_option("max_number_of_labels", tree->max_num_labels)
               .default_value(10)
               .help("max number of unique label"))
      .add(make_option("leaf_example_multiplier", tree->leaf_example_multiplier)
               .default_value(1)
               .help("multiplier on examples per leaf (default = log nodes)"))
      .add(make_option("alpha", tree->alpha).default_value(0.1f).help("Alpha"))
      .add(make_option("dream_repeats", tree->dream_repeats)
               .default_value(1)
               .help("number of dream operations per example (default = 1)"))
      .add(make_option("top_K", tree->top_K).default_value(1).help("top K prediction error (default 1)"))
      .add(make_option("learn_at_leaf", tree->learn_at_leaf).help("whether or not learn at leaf (defualt = True)"))
      .add(make_option("oas", tree->oas).help("use oas at the leaf"))
      .add(make_option("dream_at_update", tree->dream_at_update)
               .default_value(0)
               .help("turn on dream operations at reward based update as well"))
      .add(make_option("online", tree->online).help("turn on dream operations at reward based update as well"));
  options.add_and_parse(new_options);
  if (!tree->max_nodes)
  {
    return nullptr;
  }

  tree->all = &all;
  tree->current_pass = 0;
  tree->final_pass = all.numpasses;

  tree->max_leaf_examples = (size_t)(tree->leaf_example_multiplier * (log(tree->max_nodes) / log(2)));

  init_tree(*tree);

  if (!all.quiet)
    all.trace_message << "memory_tree:"
                      << " "
                      << "max_nodes = " << tree->max_nodes << " "
                      << "max_leaf_examples = " << tree->max_leaf_examples << " "
                      << "alpha = " << tree->alpha << " "
                      << "oas = " << tree->oas << " "
                      << "online =" << tree->online << " " << std::endl;

  size_t num_learners = 0;

  // multi-class classification
  if (tree->oas == false)
  {
    num_learners = tree->max_nodes + 1;
    learner<memory_tree, example>& l =
        init_multiclass_learner(tree, as_singleline(setup_base(options, all)), learn, predict, all.p, num_learners);
    // srand(time(0));
    l.set_save_load(save_load_memory_tree);
    l.set_end_pass(end_pass);
    l.set_finish(finish);

    return make_base(l);
  }  // multi-label classification
  else
  {
    num_learners = tree->max_nodes + 1 + tree->max_num_labels;
    learner<memory_tree, example>& l = init_learner(
        tree, as_singleline(setup_base(options, all)), learn, predict, num_learners, prediction_type::multilabels);

    // all.p->lp = MULTILABEL::multilabel;
    // all.label_type = label_type::multi;
    // all.delete_prediction = MULTILABEL::multilabel.delete_label;
    // srand(time(0));
    l.set_end_pass(end_pass);
    l.set_save_load(save_load_memory_tree);
    // l.set_end_pass(end_pass);
    l.set_finish(finish);

    all.p->lp = MULTILABEL::multilabel;
    all.label_type = label_type::multi;
    all.delete_prediction = MULTILABEL::multilabel.delete_label;

    return make_base(l);
  }
}
