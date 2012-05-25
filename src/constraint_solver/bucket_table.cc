// Copyright 2011-2012 Jean Charles Régin
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


#include "base/hash.h"
#include "base/int-type.h"
#include "base/int-type-indexed-vector.h"
#include "base/map-util.h"
#include "base/scoped_ptr.h"
#include "base/stl_util.h"
#include "constraint_solver/constraint_solveri.h"
#include "constraint_solver/constraint_solver.h"
#include "util/vector_map.h"

namespace operations_research {
namespace {
// ****************************************************************************
//
// GAC-4 Revisited (c) Jean-Charles Régin 2012
//
// ****************************************************************************
class Ac4TableConstraint;

class IndexedTable {
 public:
  IndexedTable(const IntTupleSet& table)
  : tuples_of_indices_(table.NumTuples() * table.Arity()),
    value_map_per_variable_(table.Arity()),
    num_tuples_per_value_(table.Arity()),
    arity_(table.Arity()),
    num_tuples_(table.NumTuples()) {
    for (int i = 0; i < arity_; i++) {
      num_tuples_per_value_[i].resize(table.NumDifferentValuesInColumn(i), 0);
      for (int t = 0; t < table.NumTuples(); t++) {
        const int64 val = table.Value(t, i);
        if (!value_map_per_variable_[i].Contains(val)) {
          value_map_per_variable_[i].Add(val);
        }
        const int index = IndexFromValue(i, val);
        tuples_of_indices_[t * arity_ + i] = index;
        num_tuples_per_value_[i][index]++;
      }
    }
  }

  ~IndexedTable() {}

  int NumVars() const { return arity_; }

  int ValueIndex(int tuple_index, int var_index) const {
    return tuples_of_indices_[tuple_index * arity_ + var_index];
  }

  int IndexFromValue(int var_index, int64 value) const {
    return value_map_per_variable_[var_index].Index(value);
  }

  int64 ValueFromIndex(int var_index, int value_index) const {
    return value_map_per_variable_[var_index].Element(value_index);
  }

  bool IsValueValid(int var_index, int64 value) const {
    return value_map_per_variable_[var_index].Contains(value);
  }

  int NumTuplesContainingValueIndex(int var_index, int value_index) const {
    return num_tuples_per_value_[var_index][value_index];
  }

  int NumTuples() const {
    return num_tuples_;
  }

  int NumDifferentValuesInColumn(int var_index) const {
    return num_tuples_per_value_[var_index].size();
  }

 private:
  std::vector<int> tuples_of_indices_;
  std::vector<VectorMap<int64> > value_map_per_variable_;
  // number of tuples per value
  std::vector<std::vector<int> > num_tuples_per_value_;
  const int arity_;
  const int num_tuples_;
};

class SwapList {
 public:
  SwapList(const int capacity)
  : elements_(new int[capacity]),
    num_elements_(0),
    capacity_(capacity) {}

  ~SwapList() {}

  int Size() const { return num_elements_.Value(); }

  int Capacity() const { return capacity_; }

  int operator[](int i) const {
    DCHECK_LT(i, capacity_);
    return elements_[i];
  }

  int push_back(Solver* const solver, int elt) {
    const int size = num_elements_.Value();
    DCHECK_LT(size, capacity_);
    elements_[size] = elt;
    num_elements_.Incr(solver);
    return size;
  }

  void push_back_from_index(Solver* const solver,
                            int i,
                            int iElt, int endBackElt) {
    elements_[i] = endBackElt;
    elements_[num_elements_.Value()] = iElt;
    num_elements_.Incr(solver);
  }

  int end_back() const { return elements_[num_elements_.Value()]; }

  int back() const { return elements_[num_elements_.Value() - 1]; }

  void erase(Solver* const solver,
             int i,
             int iElt,
             int backElt,
             int* posElt,
             int* posBack) {
    num_elements_.Decr(solver);
    elements_[num_elements_.Value()] = iElt;
    elements_[i] = backElt;
    *posElt = num_elements_.Value();
    *posBack = i;
  }

  void clear(Solver* const solver) {
    num_elements_.SetValue(solver, 0);
  }

 private:
  friend class Ac4TableConstraint;
  scoped_array<int> elements_; // set of elements.
  NumericalRev<int> num_elements_; // number of elements in the set.
  const int capacity_;
};

class Ac4TableConstraint : public Constraint {
 public:
  Ac4TableConstraint(Solver* const solver,
                     IndexedTable* const table,
                     const std::vector<IntVar*>& vars)
      : Constraint(solver),
        vars_(table->NumVars()),
        table_(table),
        tuples_to_add_(table->NumTuples()),
        delta_of_value_indices_(table->NumTuples()),
        num_variables_(table->NumVars()) {
    for (int var_index = 0; var_index < table->NumVars(); var_index++) {
      vars_[var_index] = new Var(solver, vars[var_index], var_index, table);
    }
  }

  ~Ac4TableConstraint() {
    STLDeleteElements(&vars_); // delete all elements of a vector
    delete table_;
  }

  void Post() {
    for (int var_index = 0; var_index < num_variables_; var_index++) {
      Demon* const demon =
          MakeConstraintDemon1(solver(),
                               this,
                               &Ac4TableConstraint::FilterX,
                               "FilterX",
                               var_index);
      vars_[var_index]->Variable()->WhenDomain(demon);
    }
  }

  void InitialPropagate() {
    Init();
    std::vector<int64> to_remove;
    // we remove from the domain the values that are not in the table,
    // or that have no supporting tuples.
    for (int var_index = 0; var_index < num_variables_; var_index++) {
      to_remove.clear();
      IntVarIterator* const it = vars_[var_index]->DomainIterator();
      for (it->Init(); it->Ok(); it->Next()) {
        const int64 value = it->Value();
        if (!table_->IsValueValid(var_index, value) ||
            !vars_[var_index]->AsActiveTuplesForValueIndex(
                table_->IndexFromValue(var_index, value))) {
          to_remove.push_back(value);
        }
      }
      vars_[var_index]->Variable()->RemoveValues(to_remove);
    }
    //    RemoveUnsupportedValues();
  }

  void FilterX(int var_index) {
    ComputeDeltaDomain(var_index);
    if (CheckResetProperty(var_index)) {
      Reset(var_index);
    }
    const int delta_size = delta_of_value_indices_.size();
    for (int k = 0; k < delta_size; k++) {
      PropagateDeletedValue(var_index, delta_of_value_indices_[k]);
    }
  }

 private:
  class Var {
   public:
    Var(Solver* const solver,
        IntVar* var,
        const int var_index,
        IndexedTable* const table)
        : solver_(solver),
          tuples_per_value_(table->NumDifferentValuesInColumn(var_index)),
          active_values_(tuples_per_value_.size()),
          index_in_active_values_(tuples_per_value_.size()),
          var_(var),
          domain_iterator_(var->MakeDomainIterator(true)),
          delta_domain_iterator_(var->MakeHoleIterator(true)),
          reverse_tuples_(table->NumTuples()) {
      for (int value_index = 0;
           value_index < tuples_per_value_.size();
           value_index++) {
        tuples_per_value_[value_index] = new SwapList(
            table->NumTuplesContainingValueIndex(var_index, value_index));
        index_in_active_values_[value_index] =
            active_values_.push_back(solver_, value_index);
      }
    }

    ~Var() {
      STLDeleteElements(&tuples_per_value_); // delete all elements of a vector
    }

    IntVar* Variable() const {
      return var_;
    }

    IntVarIterator* DomainIterator() const {
      return domain_iterator_;
    }

    IntVarIterator* DeltaDomainIterator() const {
      return delta_domain_iterator_;
    }

    void RemoveFromActiveValues(Solver* solver, int value_index) {
      const int back_value_index = active_values_.back();
      active_values_.erase(
          solver_,
          index_in_active_values_[value_index],
          value_index,
          back_value_index,
          &index_in_active_values_[value_index],
          &index_in_active_values_[back_value_index]);
    }

    bool AsActiveTuplesForValueIndex(int64 value_index) {
      return tuples_per_value_[value_index]->Size() > 0;
    }

   private:
    friend class Ac4TableConstraint;
    Solver* const solver_;
    // one LAA per value of the variable
    std::vector<SwapList*> tuples_per_value_;
    // list of values: having a non empty tuple list
    SwapList active_values_;
    std::vector<int> index_in_active_values_;
    IntVar* const var_;
    IntVarIterator* const domain_iterator_;
    IntVarIterator* const delta_domain_iterator_;
    // Flat tuples of value indices.
    std::vector<int> reverse_tuples_;
  };

  void RemoveUnsupportedValues() {
    // We scan values to check the ones without the supported values.
    for (int var_index = 0; var_index < num_variables_; var_index++) {
      Var* const var = vars_[var_index];
      IntVarIterator* const it = var->DomainIterator();
      int num_removed = 0;
      for (it->Init(); it->Ok(); it->Next()) {
        const int value_index = table_->IndexFromValue(var_index, it->Value());
        if (var->tuples_per_value_[value_index]->Size() == 0) {
          var->RemoveFromActiveValues(solver(), value_index);
          num_removed++;
        }
      }
      // Removed values are pushed after the lists.
      const int last_valid_value = var->active_values_.Size();
      for (int cpt = 0; cpt < num_removed; cpt++) {
        const int value_index =
            vars_[var_index]->active_values_[last_valid_value + cpt];
        const int64 removed_value =
            table_->ValueFromIndex(var_index, value_index);
        vars_[var_index]->Variable()->RemoveValue(removed_value);
      }
    }
  }

  void PropagateDeletedValue(const int var_index, const int value_index) {
    SwapList* const var_value =
        vars_[var_index]->tuples_per_value_[value_index];
    const int num_tuples_to_erase = var_value->Size();
    for (int index = 0; index < num_tuples_to_erase; index++) {
      const int erased_tuple_index = (*var_value)[0];
      // The tuple is erased for each value it contains.
      for (int var_index2 = 0; var_index2 < num_variables_; var_index2++) {
        Var* const var2 = vars_[var_index2];
        const int value_index2 =
            table_->ValueIndex(erased_tuple_index, var_index2);
        SwapList* const var_value2 = var2->tuples_per_value_[value_index2];
        const bool value_still_supported = var_value2->Size() > 1;
        const int tuple_index_in_value =
            var2->reverse_tuples_[erased_tuple_index];
        const int back_tuple_index = var_value2->back();
        var_value2->erase(
            solver(),
            tuple_index_in_value,
            erased_tuple_index,
            back_tuple_index,
            &var2->reverse_tuples_[erased_tuple_index],
            &var2->reverse_tuples_[back_tuple_index]);

        if (!value_still_supported) {
          var2->Variable()->RemoveValue(
              table_->ValueFromIndex(var_index2, value_index2));
          var2->RemoveFromActiveValues(solver(), value_index2);
        }
      }
    }
  }

  // Clean and re-add all active tuples.
  void Reset(int var_index) {
    Var* const var = vars_[var_index];
    int s = 0;
    tuples_to_add_.clear();
    IntVarIterator* const it = var->DomainIterator();
    for (it->Init(); it->Ok(); it->Next()) {
      const int v = table_->IndexFromValue(var_index, it->Value());
      SwapList* const active_tuples = var->tuples_per_value_[v];
      const int num_tuples = active_tuples->Size();
      for (int j = 0; j < num_tuples; j++) {
        tuples_to_add_.push_back((*active_tuples)[j]);
      }
    }
    // We clear all values in the Var structure.
    for (int i = 0; i < num_variables_; i++) {
      for (int k = 0; k < vars_[i]->active_values_.Size(); k++) {
        const int value_index = vars_[i]->active_values_[k];
        vars_[i]->tuples_per_value_[value_index]->clear(solver());
      }
    }

    // We add tuples one by one from the tuples_to_add_ structure.
    const int size = tuples_to_add_.size();
    for (int j = 0; j < size; j++) {
      const int tuple_index = tuples_to_add_[j];
      for (int var_index = 0; var_index < num_variables_; var_index++) {
        Var* const var = vars_[var_index];
        SwapList* const val =
            var->tuples_per_value_[table_->ValueIndex(tuple_index, var_index)];
        const int index_of_value = var->reverse_tuples_[tuple_index];
        const int ebt = val->end_back();
        var->reverse_tuples_[ebt] = index_of_value;
        var->reverse_tuples_[tuple_index] = val->Size();
        val->push_back_from_index(solver(), index_of_value, tuple_index, ebt);
      }
    }

    // Si une valeur n'a plus de support on la supprime
    RemoveUnsupportedValues();
  }

  void ComputeDeltaDomain(int var_index) {
    // calcul du delta domain de or-tools: on remplit le tableau
    // delta_ ATTENTION une val peut etre plusieurs fois dans le delta
    // : ici on s'en fout.
    IntVar* const var = vars_[var_index]->Variable();
    delta_of_value_indices_.clear();
    // we iterate the delta of the variable
    //
    // ATTENTION code for or-tools: the delta iterator does not
    // include the values between oldmin and min and the values
    // between max and oldmax
    //
    // therefore we decompose the iteration into 3 parts
    // - from oldmin to min
    // - for the deleted values between min and max
    // - from max to oldmax
    // First iteration: from oldmin to min
    const int64 oldmindomain = var->OldMin();
    const int64 mindomain = var->Min();
    for (int64 val = oldmindomain; val < mindomain; ++val) {
      if (table_->IsValueValid(var_index, val)) {
        delta_of_value_indices_.push_back(
            table_->IndexFromValue(var_index, val));
      }
    }
    // Second iteration: "delta" domain iteration
    IntVarIterator* const it = vars_[var_index]->DeltaDomainIterator();
    for (it->Init(); it->Ok(); it->Next()) {
      int64 val = it->Value();
      if (table_->IsValueValid(var_index, val)) {
        delta_of_value_indices_.push_back(
            table_->IndexFromValue(var_index, val));
      }
    }
    // Third iteration: from max to oldmax
    const int64 oldmaxdomain = var->OldMax();
    const int64 maxdomain = var->Max();
    for (int64 val = maxdomain + 1; val <= oldmaxdomain; ++val) {
      if (table_->IsValueValid(var_index, val)) {
        delta_of_value_indices_.push_back(
            table_->IndexFromValue(var_index, val));
      }
    }
  }

  bool CheckResetProperty(int var_index) {
    // retourne true si on doit faire un reset. on compte le nb de
    // tuples qui vont etre supprimés.
    Var* const var = vars_[var_index];
    int num_deleted_tuples = 0;
    for (int k = 0; k < delta_of_value_indices_.size(); k++) {
      num_deleted_tuples +=
          var->tuples_per_value_[delta_of_value_indices_[k]]->Size();
    }
    // on calcule le nombre de tuples en balayant les valeurs du
    // domaine de la var x
    int num_tuples_in_domain = 0;
    IntVarIterator* const it = var->DomainIterator();
    for (it->Init(); it->Ok(); it->Next()) {
      const int value_index = table_->IndexFromValue(var_index, it->Value());
      num_tuples_in_domain += var->tuples_per_value_[value_index]->Size();
    }
    return (num_tuples_in_domain < num_deleted_tuples);
  }

  void Init() {
    const int num_tuples = table_->NumTuples();
    for (int tuple_index = 0; tuple_index < num_tuples; tuple_index++) {
      for (int var_index = 0; var_index < num_variables_; var_index++) {
        Var* const var = vars_[var_index];
        SwapList* const active_tuples =
            var->tuples_per_value_[table_->ValueIndex(tuple_index, var_index)];
        var->reverse_tuples_[tuple_index] = active_tuples->Size();
        active_tuples->push_back(solver(), tuple_index);
      }
    }
  }

  std::vector<Var*> vars_; // variable of the constraint
  IndexedTable* const table_; // table
  // On peut le supprimer si on a un tableau temporaire d'entier qui
  // est disponible. Peut contenir tous les tuples
  std::vector<int> tuples_to_add_;
  // Temporary storage for delta of one variable.
  std::vector<int> delta_of_value_indices_;
  // Number of variables.
  const int num_variables_;
};
}  // namespace

// External API.
Constraint* BuildAc4TableConstraint(Solver* const solver,
                                    IntTupleSet& tuples,
                                    const std::vector<IntVar*>& vars,
                                    int size_bucket) {
  return solver->RevAlloc(
      new Ac4TableConstraint(solver, new IndexedTable(tuples), vars));
}
} // namespace operations_research
