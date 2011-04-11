// Copyright 2010-2011 Google
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This file implements the table constraints.

#include "base/commandlineflags.h"
#include "base/integral_types.h"
#include "base/logging.h"
#include "base/scoped_ptr.h"
#include "base/concise_iterator.h"
#include "base/map-util.h"
#include "constraint_solver/constraint_solveri.h"
#include "util/bitset.h"

DEFINE_bool(cp_use_compact_table, true,
            "Use compact table constraint when possible.");
DEFINE_bool(cp_use_small_table, true,
            "Use small compact table constraint when possible.");

namespace operations_research {
namespace {

static const int kBitsInUint64 = 64;

// ----- Positive Table Constraint -----

// Structure of the constraint:

// Tuples are indexed, we maintain a bitset for active tuples.

// For each var and each value, we maintain a bitset mask of tuples
// containing this value for this variable.

// Propagation: When a value is removed, blank all active tuples using the
// var-value mask.
// Then we scan all other variable/values to see if there is an active
// tuple that supports it.
class BasePositiveTableConstraint : public Constraint {
 public:
  BasePositiveTableConstraint(Solver* const s,
                              const IntVar* const * vars,
                              const int64* const * allowed_tuples,
                              int tuple_count,
                              int arity)
      : Constraint(s),
        tuple_count_(tuple_count),
        arity_(arity),
        vars_(new IntVar*[arity]),
        holes_(new IntVarIterator*[arity]),
        iterators_(new IntVarIterator*[arity]) {
    // Copy vars.
    memcpy(vars_.get(), vars, arity_ * sizeof(*vars));
    // Create hole iterators
    for (int i = 0; i < arity_; ++i) {
      holes_[i] = vars_[i]->MakeHoleIterator(true);
      iterators_[i] = vars_[i]->MakeDomainIterator(true);
    }
  }

  BasePositiveTableConstraint(Solver* const s,
                              const vector<IntVar*> & vars,
                              const vector<vector<int64> >& allowed_tuples)
      : Constraint(s),
        tuple_count_(allowed_tuples.size()),
        arity_(vars.size()),
        vars_(new IntVar*[arity_]),
        holes_(new IntVarIterator*[arity_]),
        iterators_(new IntVarIterator*[arity_]) {
    // Copy vars.
    memcpy(vars_.get(), vars.data(), arity_ * sizeof(*vars.data()));
    // Create hole iterators
    for (int i = 0; i < arity_; ++i) {
      holes_[i] = vars_[i]->MakeHoleIterator(true);
      iterators_[i] = vars_[i]->MakeDomainIterator(true);
    }
  }

  virtual ~BasePositiveTableConstraint() {}
 protected:
  const int tuple_count_;
  const int arity_;
  scoped_array<IntVar*> vars_;
  scoped_array<IntVarIterator*> holes_;
  scoped_array<IntVarIterator*> iterators_;
  vector<int64> to_remove_;
};

class PositiveTableConstraint : public BasePositiveTableConstraint {
 public:
  PositiveTableConstraint(Solver* const s,
                          const IntVar* const * vars,
                          const int64* const * tuples,
                          int tuple_count,
                          int arity)
      : BasePositiveTableConstraint(s, vars, tuples, tuple_count, arity),
        length_(BitLength64(tuple_count)),
        active_tuples_(new uint64[length_]),
        stamps_(new uint64[length_]) {
    masks_.clear();
    masks_.resize(arity_);
    for (int i = 0; i < tuple_count_; ++i) {
      InitializeMask(i, tuples[i]);
    }
  }

  PositiveTableConstraint(Solver* const s,
                          const vector<IntVar*> & vars,
                          const vector<vector<int64> >& tuples)
      : BasePositiveTableConstraint(s, vars, tuples),
        length_(BitLength64(tuples.size())),
        active_tuples_(new uint64[length_]),
        stamps_(new uint64[length_]) {
    masks_.clear();
    masks_.resize(arity_);
    for (int i = 0; i < tuple_count_; ++i) {
      InitializeMask(i, tuples[i].data());
    }
  }

  virtual ~PositiveTableConstraint() {
    for (int var_index = 0; var_index < arity_; ++var_index) {
      for (ConstIter<hash_map<int64, uint64*> > it(masks_[var_index]);
           !it.at_end();
           ++it) {
        delete [] it->second;
      }
    }
  }

  virtual void Post() {
    Demon* d = MakeDelayedConstraintDemon0(solver(),
                                           this,
                                           &PositiveTableConstraint::Propagate,
                                           "Propagate");
    uint64 stamp = solver()->stamp();
    DCHECK_GE(stamp, 1);
    for (int i = 0; i < arity_; ++i) {
      vars_[i]->WhenDomain(d);
      Demon* u = MakeConstraintDemon1(solver(),
                                      this,
                                      &PositiveTableConstraint::Update,
                                      "Update",
                                      i);
      vars_[i]->WhenDomain(u);
    }
    for (int i = 0; i < length_; ++i) {
      stamps_[i] = stamp - 1;
      active_tuples_[i] = ~GG_ULONGLONG(0);
    }
  }

  virtual void InitialPropagate() {
    // Build active_ structure.
    for (int var_index = 0; var_index < arity_; ++var_index) {
      for (ConstIter<hash_map<int64, uint64*> > it(masks_[var_index]);
           !it.at_end(); ++it) {
        if (!vars_[var_index]->Contains(it->first)) {
          for (int i = 0; i < length_; ++i) {
            uint64 active = active_tuples_[i] & it->second[i];
            while (active != 0) {
              int position = LeastSignificantBitPosition64(active);
              active_tuples_[i] &= ~OneBit64(position);
              active &= IntervalUp64(position + 1);
            }
          }
        }
      }
    }
    bool found_one = false;
    for (int i = 0; i < length_; ++i) {
      if (active_tuples_[i] != 0) {
        found_one = true;
        break;
      }
    }
    if (!found_one) {
      solver()->Fail();
    }
    // Remove unreached values.
    for (int var_index = 0; var_index < arity_; ++var_index) {
      const hash_map<int64, uint64*>& mask = masks_[var_index];
      IntVar* const var = vars_[var_index];
      to_remove_.clear();
      IntVarIterator* const it = iterators_[var_index];
      for (it->Init(); it->Ok(); it->Next()) {
        const int64 value = it->Value();
        if (!ContainsKey(mask, value)) {
          to_remove_.push_back(value);
        }
      }
      if (to_remove_.size() > 0) {
        var->RemoveValues(to_remove_);
      }
    }
  }

  void Propagate() {
    for (int var_index = 0; var_index < arity_; ++var_index) {
      IntVar* const var = vars_[var_index];
      to_remove_.clear();
      IntVarIterator* const it = iterators_[var_index];
      for (it->Init(); it->Ok(); it->Next()) {
        const int64 value = it->Value();
        if (!Supported(var_index, value)) {
          to_remove_.push_back(value);
        }
      }
      if (to_remove_.size() > 0) {
        var->RemoveValues(to_remove_);
      }
    }
  }

  void Update(int index) {
    const hash_map<int64, uint64*>& mask = masks_[index];
    IntVar* const var = vars_[index];
    const int64 oldmax = var->OldMax();
    const int64 vmin = var->Min();
    const int64 vmax = var->Max();
    for (int64 value = var->OldMin(); value < vmin; ++value) {
      BlankActives(FindPtrOrNull(mask, value));
    }
    for (holes_[index]->Init(); holes_[index]->Ok(); holes_[index]->Next()) {
      BlankActives(FindPtrOrNull(mask, holes_[index]->Value()));
    }
    for (int64 value = vmax + 1; value <= oldmax; ++value) {
      BlankActives(FindPtrOrNull(mask, value));
    }
  }

  void BlankActives(uint64* const mask) {
    if (mask != NULL) {
      bool empty = true;
      for (int offset = 0; offset < length_; ++offset) {
        if ((mask[offset] & active_tuples_[offset]) != 0) {
          const uint64 current_stamp = solver()->stamp();
          if (stamps_[offset] < current_stamp) {
            stamps_[offset] = current_stamp;
            solver()->SaveValue(&active_tuples_[offset]);
          }
          active_tuples_[offset] &= ~mask[offset];
        }
        if (active_tuples_[offset] != 0) {
          empty = false;
        }
      }
      if (empty) {
        solver()->Fail();
      }
    }
  }

  bool Supported(int var_index, int64 value) {
    DCHECK_GE(var_index, 0);
    DCHECK_LT(var_index, arity_);
    DCHECK(ContainsKey(masks_[var_index], value));
    uint64* const mask = masks_[var_index][value];
    for (int offset = 0; offset < length_; ++offset) {
      if ((mask[offset] & active_tuples_[offset]) != 0LL) {
        return true;
      }
    }
    return false;
  }

  virtual string DebugString() const { return "PositiveTableConstraint"; }

 protected:
  void InitializeMask(int tuple_index, const int64* const tuple) {
    for (int var_index = 0; var_index < arity_; ++var_index) {
      const int64 value = tuple[var_index];
      uint64* mask = FindPtrOrNull(masks_[var_index], value);
      if (mask == NULL) {
        mask = new uint64[length_];
        memset(mask, 0, length_ * sizeof(*mask));
        masks_[var_index][value] = mask;
      }
      SetBit64(mask, tuple_index);
    }
  }

  const int length_;
  // TODO(user): create bitset64 class and use it.
  scoped_array<uint64> active_tuples_;
  scoped_array<uint64> stamps_;
  vector<hash_map<int64, uint64*> > masks_;
};

// ----- Compact Table. -----

class CompactPositiveTableConstraint : public BasePositiveTableConstraint {
 public:
  CompactPositiveTableConstraint(Solver* const s,
                                 const IntVar* const * vars,
                                 const int64* const * tuples,
                                 int tuple_count,
                                 int arity)
      : BasePositiveTableConstraint(s, vars, tuples, tuple_count, arity),
        length_(BitLength64(tuple_count)),
        active_tuples_(new uint64[length_]),
        stamps_(new uint64[length_]),
        tuples_(new int64*[tuple_count_]),
        original_min_(new int64[arity_]),
        temp_mask_(new uint64[length_]),
        demon_(NULL) {
    // Copy tuples
    for (int i = 0; i < tuple_count_; ++i) {
      tuples_[i] = new int64[arity_];
      memcpy(tuples_[i], tuples[i], arity_ * sizeof(*tuples[i]));
    }
  }

  CompactPositiveTableConstraint(Solver* const s,
                                 const vector<IntVar*> & vars,
                                 const vector<vector<int64> >& tuples)
      : BasePositiveTableConstraint(s, vars, tuples),
        length_(BitLength64(tuples.size())),
        active_tuples_(new uint64[length_]),
        stamps_(new uint64[length_]),
        tuples_(new int64*[tuple_count_]),
        original_min_(new int64[arity_]),
        temp_mask_(new uint64[length_]),
        demon_(NULL) {
    // Copy tuples
    for (int i = 0; i < tuple_count_; ++i) {
      CHECK_EQ(arity_, tuples[i].size());
      tuples_[i] = new int64[arity_];
      memcpy(tuples_[i], tuples[i].data(), arity_ * sizeof(tuples[i][0]));
    }
  }

  virtual ~CompactPositiveTableConstraint() {
    for (int i = 0; i < tuple_count_; ++i) {
      delete[] tuples_[i];
      tuples_[i] = NULL;
    }
  }

  virtual void Post() {
    demon_ = MakeDelayedConstraintDemon0(
        solver(),
        this,
        &CompactPositiveTableConstraint::Propagate,
        "Propagate");
    uint64 stamp = solver()->stamp();
    DCHECK_GE(stamp, 1);
    for (int i = 0; i < arity_; ++i) {
      //      vars_[i]->WhenDomain(d);
      Demon* u = MakeConstraintDemon1(solver(),
                                      this,
                                      &CompactPositiveTableConstraint::Update,
                                      "Update",
                                      i);
      vars_[i]->WhenDomain(u);
    }
    for (int i = 0; i < length_; ++i) {
      stamps_[i] = stamp - 1;
      active_tuples_[i] = 0;
    }
  }

  bool IsTupleSupported(int tuple_index) {
    for (int var_index = 0; var_index < arity_; ++var_index) {
      const int64 value = tuples_[tuple_index][var_index];
      if (!vars_[var_index]->Contains(value)) {
        return false;
      }
    }
    return true;
  }

  void BuildStructures() {
    // Build active_ structure.
    bool found_one = false;
    for (int tuple_index = 0; tuple_index < tuple_count_; ++tuple_index) {
      if (IsTupleSupported(tuple_index)) {
        SetBit64(active_tuples_.get(), tuple_index);
        found_one = true;
      }
    }
    if (!found_one) {
      solver()->Fail();
    }
  }

  void BuildMasks() {
    // Build masks.
    masks_.clear();
    masks_.resize(arity_);
    for (int i = 0; i < arity_; ++i) {
      original_min_[i] = vars_[i]->Min();
      const int64 span = vars_[i]->Max() - original_min_[i] + 1;
      masks_[i].resize(span);
      for (int j = 0; j < span; ++j) {
        masks_[i][j] = NULL;
      }
    }
  }

  void FillMasks() {
    for (int tuple_index = 0; tuple_index < tuple_count_; ++tuple_index) {
      if (IsBitSet64(active_tuples_.get(), tuple_index)) {
        for (int var_index = 0; var_index < arity_; ++var_index) {
          const int64 value_index =
              tuples_[tuple_index][var_index] - original_min_[var_index];
          DCHECK_GE(value_index, 0);
          DCHECK_LT(value_index, masks_[var_index].size());
          uint64* mask = masks_[var_index][value_index];
          if (!mask) {
            mask = solver()->RevAlloc(new uint64[length_]);
            memset(mask, 0, length_ * sizeof(*mask));
            masks_[var_index][value_index] = mask;
          }
          SetBit64(mask, tuple_index);
        }
      }
    }
  }

  void ComputeMasksBoundaries() {
    // Store boundaries of non zero parts of masks.
    starts_.clear();
    starts_.resize(arity_);
    ends_.clear();
    ends_.resize(arity_);
    supports_.clear();
    supports_.resize(arity_);
    for (int var_index = 0; var_index < arity_; ++var_index) {
      const int64 span = vars_[var_index]->Max() - original_min_[var_index] + 1;
      starts_[var_index].resize(span);
      ends_[var_index].resize(span);
      supports_[var_index].resize(span);
      for (int value_index = 0; value_index < span; ++value_index) {
        const uint64* const mask = masks_[var_index][value_index];
        if (mask != NULL) {
          starts_[var_index][value_index] = 0;
          while (!mask[starts_[var_index][value_index]]) {
            starts_[var_index][value_index]++;
          }
          supports_[var_index][value_index] = starts_[var_index][value_index];
          ends_[var_index][value_index] = length_ - 1;
          while (!mask[ends_[var_index][value_index]]) {
            ends_[var_index][value_index]--;
          }
        }
      }
    }
  }

  void RemoveUnsupportedValues() {
    // remove unreached values.
    for (int var_index = 0; var_index < arity_; ++var_index) {
      IntVar* const var = vars_[var_index];
      to_remove_.clear();
      IntVarIterator* const it = iterators_[var_index];
      for (it->Init(); it->Ok(); it->Next()) {
        const int64 value = it->Value();
        if (!masks_[var_index][value - original_min_[var_index]]) {
          to_remove_.push_back(value);
        }
      }
      if (to_remove_.size() > 0) {
        var->RemoveValues(to_remove_);
      }
    }
  }

  virtual void InitialPropagate() {
    BuildStructures();
    BuildMasks();
    FillMasks();
    ComputeMasksBoundaries();
    RemoveUnsupportedValues();
  }

  bool Supported(int var_index, int64 value_index) {
    DCHECK_GE(var_index, 0);
    DCHECK_LT(var_index, arity_);
    DCHECK_GE(value_index, 0);
    DCHECK(masks_[var_index][value_index]);
    const uint64* const mask = masks_[var_index][value_index];
    const int support = supports_[var_index][value_index];
    if ((mask[support] & active_tuples_[support]) != 0) {
      return true;
    }
    const int loop_end = ends_[var_index][value_index];
    for (int offset = starts_[var_index][value_index];
         offset <= loop_end;
         ++offset) {
      if ((mask[offset] & active_tuples_[offset]) != 0) {
        supports_[var_index][value_index] = offset;
        return true;
      }
    }
    return false;
  }

  void Propagate() {
    // This methods scans all values of all variables to see if they
    // are still supported.
    // This method is not attached to any particular variable, but is pushed
    // at a delayed priority when Update(var_index) deems it necessary.
    const uint64 current_stamp = solver()->stamp();
    memset(temp_mask_.get(), 0, length_ * sizeof(*temp_mask_.get()));
    for (int var_index = 0; var_index < arity_; ++var_index) {
      to_remove_.clear();
      IntVarIterator* const it = iterators_[var_index];
      for (it->Init(); it->Ok(); it->Next()) {
        const int64 value_index = it->Value() - original_min_[var_index];
        if (!Supported(var_index, value_index)) {
          to_remove_.push_back(it->Value());
        }
      }
      if (to_remove_.size() > 0) {
        vars_[var_index]->RemoveValues(to_remove_);
        // Actively remove unsupported bitsets from active_tuples_.
        for (int offset = 0; offset < length_; ++offset) {
          temp_mask_[offset] = 0;
        }
        for (ConstIter<vector<int64> > it(to_remove_); !it.at_end(); ++it) {
          const int64 value_index = (*it) - original_min_[var_index];
          const uint64* const mask = masks_[var_index][value_index];
          DCHECK(mask);
          UpdateTempMask(mask,
                         starts_[var_index][value_index],
                         ends_[var_index][value_index]);
        }
        for (int offset = 0; offset < length_; ++offset) {
          if ((temp_mask_[offset] & active_tuples_[offset]) != 0) {
            if (stamps_[offset] < current_stamp) {
              stamps_[offset] = current_stamp;
              solver()->SaveValue(&active_tuples_[offset]);
            }
            active_tuples_[offset] &= ~temp_mask_[offset];
          }
        }
      }
    }
    for (int offset = 0; offset < length_; ++offset) {
      if (active_tuples_[offset]) {
        return;
      }
    }
    solver()->Fail();
  }

  void UpdateTempMask(const uint64* const mask, int start, int end) {
    for (int offset = start; offset <= end; ++offset) {
      temp_mask_[offset] |= mask[offset];
    }
  }

  void Update(int var_index) {
    // This method will update the set of active tuples by masking out all
    // tuples attached to values of the variables that have been removed.

    // We first collect the complete set of tuples to blank out in temp_mask_.
    IntVar* const var = vars_[var_index];
    const int64 omin = original_min_[var_index];
    bool changed = false;
    memset(temp_mask_.get(), 0, length_ * sizeof(*temp_mask_.get()));
    const int64 oldmax = var->OldMax();
    const int64 vmin = var->Min();
    const int64 vmax = var->Max();
    for (int64 value = var->OldMin(); value < vmin; ++value) {
      const int64 value_index = value  - omin;
      const uint64* const mask = masks_[var_index][value_index];
      if (mask) {
        UpdateTempMask(mask,
                       starts_[var_index][value_index],
                       ends_[var_index][value_index]);
      }
    }
    IntVarIterator* const hole = holes_[var_index];
    for (hole->Init(); hole->Ok(); hole->Next()) {
      const int64 value_index = hole->Value() - omin;
      const uint64* const mask = masks_[var_index][value_index];
      if (mask) {
        UpdateTempMask(mask,
                       starts_[var_index][value_index],
                       ends_[var_index][value_index]);
      }
    }
    for (int64 value = vmax + 1; value <= oldmax; ++value) {
      const int64 value_index = value  - omin;
      const uint64* const mask = masks_[var_index][value_index];
      if (mask) {
        UpdateTempMask(mask,
                       starts_[var_index][value_index],
                       ends_[var_index][value_index]);
      }
    }
    // Then we apply this mask to active_tuples_.
    for (int offset = 0; offset < length_; ++offset) {
      if ((temp_mask_[offset] & active_tuples_[offset]) != 0) {
        const uint64 current_stamp = solver()->stamp();
        if (stamps_[offset] < current_stamp) {
          stamps_[offset] = current_stamp;
          solver()->SaveValue(&active_tuples_[offset]);
        }
        active_tuples_[offset] &= ~temp_mask_[offset];
        changed = true;
      }
    }
    // And check active_tuples_ is still not empty, we fail otherwise.
    for (int offset = 0; offset < length_; ++offset) {
      if (active_tuples_[offset]) {
        // We push the propagate method only if something has changed.
        if (changed) {
          Enqueue(demon_);
        }
        return;
      }
    }
    solver()->Fail();
  }

  virtual string DebugString() const { return "PositiveTableConstraint"; }

 private:
  // Length of bitsets in double words.
  const int length_;
  // Bitset of active tuples.
  scoped_array<uint64> active_tuples_;
  // Array of stamps, one per 64 tuples.
  scoped_array<uint64> stamps_;
  // All allowed tuples.
  scoped_array<int64*> tuples_;
  // The masks per value per variable.
  vector<vector<uint64*> > masks_;
  // The min on the vars at creation time.
  scoped_array<int64> original_min_;
  // The starts of active bitsets.
  vector<vector<int> > starts_;
  // The ends of the active bitsets.x
  vector<vector<int> > ends_;
  // A temporary mask use for computation.
  scoped_array<uint64> temp_mask_;
  // The portion of the active tuples supporting each value per variable.
  vector<vector<int> > supports_;
  Demon* demon_;
};

// ----- Small Compact Table. -----

// TODO(user): regroup code with CompactPositiveTableConstraint.

class SmallCompactPositiveTableConstraint : public BasePositiveTableConstraint {
 public:
  SmallCompactPositiveTableConstraint(Solver* const s,
                                      const IntVar* const * vars,
                                      const int64* const * tuples,
                                      int tuple_count,
                                      int arity)
      : BasePositiveTableConstraint(s, vars, tuples, tuple_count, arity),
        active_tuples_(0),
        stamp_(0),
        tuples_(new int64*[tuple_count_]),
        masks_(new uint64*[arity_]),
        original_min_(new int64[arity_]),
        demon_(NULL) {
    CHECK_GE(tuple_count_, 0);
    CHECK_LE(tuple_count_, kBitsInUint64);
    CHECK_GE(arity_, 0);
    // Copy tuples
    for (int i = 0; i < tuple_count_; ++i) {
      tuples_[i] = new int64[arity_];
      memcpy(tuples_[i], tuples[i], arity_ * sizeof(*tuples[i]));
    }
    // Zero masks
    memset(masks_.get(), 0, arity_ * sizeof(*masks_.get()));
  }

  SmallCompactPositiveTableConstraint(Solver* const s,
                                      const vector<IntVar*> & vars,
                                      const vector<vector<int64> >& tuples)
      : BasePositiveTableConstraint(s, vars, tuples),
        active_tuples_(0),
        stamp_(0),
        tuples_(new int64*[tuple_count_]),
        masks_(new uint64*[arity_]),
        original_min_(new int64[arity_]),
        demon_(NULL) {
    CHECK_GE(tuple_count_, 0);
    CHECK_GE(arity_, 0);
    CHECK_LE(tuples.size(), kBitsInUint64);
    // Copy tuples
    for (int i = 0; i < tuple_count_; ++i) {
      CHECK_EQ(arity_, tuples[i].size());
      tuples_[i] = new int64[arity_];
      memcpy(tuples_[i], tuples[i].data(), arity_ * sizeof(tuples[i][0]));
    }
    // Zero masks
    memset(masks_.get(), 0, arity_ * sizeof(*masks_.get()));
  }

  virtual ~SmallCompactPositiveTableConstraint() {
    for (int i = 0; i < tuple_count_; ++i) {
      delete[] tuples_[i];
      tuples_[i] = NULL;
    }
    for (int i = 0; i < arity_; ++i) {
      delete [] masks_[i];
      masks_[i] = NULL;
    }
  }

  virtual void Post() {
    demon_ = MakeDelayedConstraintDemon0(
        solver(),
        this,
        &SmallCompactPositiveTableConstraint::Propagate,
        "Propagate");
    for (int i = 0; i < arity_; ++i) {
      if (!vars_[i]->Bound()) {
        Demon* const update_demon = MakeConstraintDemon1(
            solver(),
            this,
            &SmallCompactPositiveTableConstraint::Update,
            "Update",
            i);
        vars_[i]->WhenDomain(update_demon);
      }
    }
    stamp_ = solver()->stamp() - 1;
  }

  void InitMasks() {
    // Build masks.
    for (int i = 0; i < arity_; ++i) {
      original_min_[i] = vars_[i]->Min();
      const int64 span = vars_[i]->Max() - original_min_[i] + 1;
      masks_[i] = new uint64[span];
      memset(masks_[i], 0, span * sizeof(*masks_[i]));
    }
  }

  bool IsTupleSupported(int tuple_index) {
    for (int var_index = 0; var_index < arity_; ++var_index) {
      const int64 value = tuples_[tuple_index][var_index];
      if (!vars_[var_index]->Contains(value)) {
        return false;
      }
    }
    return true;
  }

  void ComputeActiveTuples() {
    active_tuples_ = 0;
   // Compute active_tuples_ and update masks.
    for (int tuple_index = 0; tuple_index < tuple_count_; ++tuple_index) {
      if (IsTupleSupported(tuple_index)) {
        const uint64 local_mask = OneBit64(tuple_index);
        active_tuples_ |= local_mask;
        for (int var_index = 0; var_index < arity_; ++var_index) {
          const int64 value_index =
              tuples_[tuple_index][var_index] - original_min_[var_index];
          masks_[var_index][value_index] |= local_mask;
        }
      }
    }
    if (!active_tuples_) {
      solver()->Fail();
    }
  }

  void RemoveUnsupportedValues() {
    // remove unreached values.
    for (int var_index = 0; var_index < arity_; ++var_index) {
      IntVar* const var = vars_[var_index];
      const int64 original_min = original_min_[var_index];
      to_remove_.clear();
      IntVarIterator* const it = iterators_[var_index];
      for (it->Init(); it->Ok(); it->Next()) {
        const int64 value = it->Value();
        if (masks_[var_index][value - original_min] == 0) {
          to_remove_.push_back(value);
        }
      }
      if (to_remove_.size() > 0) {
        var->RemoveValues(to_remove_);
      }
    }
  }

  virtual void InitialPropagate() {
    InitMasks();
    ComputeActiveTuples();
    RemoveUnsupportedValues();
  }

  void SaveActives() {
    const uint64 current_stamp = solver()->stamp();
    if (stamp_ < current_stamp) {
      stamp_ = current_stamp;
      solver()->SaveValue(&active_tuples_);
    }
  }

  void Propagate() {
    // This methods scans all the values of all the variables to see if they
    // are still supported.
    // This method is not attached to any particular variable, but is pushed
    // at a delayed priority and awakened by Update(var_index).

    // We cache active_tuples_.
    const uint64 actives = active_tuples_;

    // We scan all variables and check their domains.
    for (int var_index = 0; var_index < arity_; ++var_index) {
      const uint64* const var_mask = masks_[var_index];
      const int64 original_min = original_min_[var_index];
      IntVar* const var = vars_[var_index];
      if (var->Bound()) {
        if ((var_mask[var->Min() - original_min] & actives) == 0) {
          solver()->Fail();
        }
      } else {
        to_remove_.clear();
        IntVarIterator* const it = iterators_[var_index];
        for (it->Init(); it->Ok(); it->Next()) {
          const int64 value = it->Value();
          if ((var_mask[value - original_min] & actives) == 0) {
            to_remove_.push_back(value);
          }
        }
        if (to_remove_.size() == var->Size()) {
          solver()->Fail();
        } else if (to_remove_.size() > 0) {
          vars_[var_index]->RemoveValues(to_remove_);
        }
      }
    }
  }

  void Update(int var_index) {
    // This method updates the set of active tuples by masking out all
    // tuples attached to values of the variables that have been removed.

    // We first collect the complete set of tuples to blank out in temp_mask.
    IntVar* const var = vars_[var_index];
    const int64 original_min = original_min_[var_index];
    uint64 temp_mask = 0;
    const uint64* const var_mask = masks_[var_index];
    const int64 oldmax = var->OldMax();
    const int64 vmin = var->Min();
    const int64 vmax = var->Max();

    for (int64 value = var->OldMin(); value < vmin; ++value) {
      temp_mask |= var_mask[value - original_min];
    }
    IntVarIterator* const hole = holes_[var_index];
    for (hole->Init(); hole->Ok(); hole->Next()) {
      temp_mask |= var_mask[hole->Value() - original_min];
    }
    for (int64 value = vmax + 1; value <= oldmax; ++value) {
      temp_mask |= var_mask[value - original_min];
    }
    // Then we apply this mask to active_tuples_.
    if (temp_mask & active_tuples_) {
      SaveActives();
      active_tuples_ &= ~temp_mask;
      if (active_tuples_) {
        Enqueue(demon_);
      } else {
        solver()->Fail();
      }
    }
  }

  virtual string DebugString() const {
    return "SmallCompactPositiveTableConstraint";
  }
 private:
  // Bitset of active tuples.
  uint64 active_tuples_;
  // Stamp of the active_tuple bitset.
  uint64 stamp_;
  // All allowed tuples.
  scoped_array<int64*> tuples_;
  // The masks per value per variable.
  scoped_array<uint64*> masks_;
  // The min on the vars at creation time.
  scoped_array<int64> original_min_;
  Demon* demon_;
};


bool HasCompactDomains(const IntVar* const * vars, int arity) {
  int64 sum_of_spans = 0LL;
  int64 sum_of_sizes = 0LL;
  for (int i = 0; i < arity; ++i) {
    const int64 vmin = vars[i]->Min();
    const int64 vmax = vars[i]->Max();
    sum_of_sizes += vars[i]->Size();
    sum_of_spans += vmax - vmin + 1;
  }
  return sum_of_spans < 4 * sum_of_sizes;
}
}  // namespace

Constraint* Solver::MakeAllowedAssignments(const IntVar* const * vars,
                                           const int64* const * tuples,
                                           int tuple_count,
                                           int arity) {
  if (FLAGS_cp_use_compact_table && HasCompactDomains(vars, arity)) {
    if (tuple_count < kBitsInUint64 && FLAGS_cp_use_small_table) {
      return RevAlloc(new SmallCompactPositiveTableConstraint(this,
                                                              vars,
                                                              tuples,
                                                              tuple_count,
                                                              arity));
    } else {
      return RevAlloc(new CompactPositiveTableConstraint(this,
                                                         vars,
                                                         tuples,
                                                         tuple_count,
                                                         arity));
    }
  }
  return RevAlloc(new PositiveTableConstraint(this,
                                              vars,
                                              tuples,
                                              tuple_count,
                                              arity));
}

Constraint* Solver::MakeAllowedAssignments(
    const vector<IntVar*>& vars, const vector<vector<int64> >& tuples) {
  if (FLAGS_cp_use_compact_table
      && HasCompactDomains(vars.data(), vars.size())) {
    if (tuples.size() < kBitsInUint64 && FLAGS_cp_use_small_table) {
      return RevAlloc(
          new SmallCompactPositiveTableConstraint(this, vars, tuples));
    } else {
      return RevAlloc(new CompactPositiveTableConstraint(this, vars, tuples));
    }
  }
  return RevAlloc(new PositiveTableConstraint(this, vars, tuples));
}

// ---------- Deterministic Finite Automaton ----------

// This constraint implements a finite automaton when transitions are
// the values of the variables in the array.
// that is state[i+1] = transition[var[i]][state[i]] if
// (state[i], var[i],  state[i+1]) in the transition table.
// There is only one possible transition for a state/value pair.
class TransitionConstraint : public Constraint {
 public:
  static const int kStatePosition;
  static const int kNextStatePosition;
  static const int kTransitionTupleSize;
  TransitionConstraint(Solver* const s,
                       const vector<IntVar*>& vars,
                       const vector<vector<int64> >& transition_table,
                       int64 initial_state,
                       const vector<int64>& final_states)
      : Constraint(s),
        vars_(vars),
        transition_table_(transition_table),
        initial_state_(initial_state),
        final_states_(final_states) {}

  virtual ~TransitionConstraint() {}

  virtual void Post() {
    Solver* const s = solver();
    int64 state_min = kint64max;
    int64 state_max = kint64min;
    const int nb_vars = vars_.size();
    for (int i = 0; i < transition_table_.size(); ++i) {
      CHECK_EQ(kTransitionTupleSize, transition_table_[i].size());
      state_max = std::max(state_max, transition_table_[i][kStatePosition]);
      state_max = std::max(state_max, transition_table_[i][kNextStatePosition]);
      state_min = std::min(state_min, transition_table_[i][kStatePosition]);
      state_min = std::min(state_min, transition_table_[i][kNextStatePosition]);
    }

    vector<IntVar*> states;
    states.push_back(s->MakeIntConst(initial_state_));
    for (int var_index = 1; var_index < nb_vars; ++var_index) {
      states.push_back(s->MakeIntVar(state_min, state_max));
    }
    states.push_back(s->MakeIntVar(final_states_));
    CHECK_EQ(nb_vars + 1, states.size());

    for (int var_index = 0; var_index < nb_vars; ++var_index) {
      vector<IntVar*> tmp_vars;
      tmp_vars.push_back(states[var_index]);
      tmp_vars.push_back(vars_[var_index]);
      tmp_vars.push_back(states[var_index + 1]);
      s->AddConstraint(s->MakeAllowedAssignments(tmp_vars, transition_table_));
    }
  }

  virtual void InitialPropagate() {}
 private:
  // Variable representing transitions between states. See header file.
  const vector<IntVar*> vars_;
  // The transition as tuples (state, value, next_state).
  const vector<vector<int64> > transition_table_;
  // The initial state before the first transition.
  const int64 initial_state_;
  // Vector of final state after the last transision.
  const vector<int64> final_states_;
};

// TODO(user): create transition struct.

const int TransitionConstraint::kStatePosition = 0;
const int TransitionConstraint::kNextStatePosition = 2;
const int TransitionConstraint::kTransitionTupleSize = 3;

Constraint* Solver::MakeTransitionConstraint(
    const vector<IntVar*>& vars,
    const vector<vector<int64> >& transition_table,
    int64 initial_state,
    const vector<int64>& final_states) {
  return RevAlloc(new TransitionConstraint(this, vars, transition_table,
                                           initial_state, final_states));
}

}  // namespace operations_research
