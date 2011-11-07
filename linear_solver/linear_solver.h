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
//                                                      (Laurent Perron).
//
// A C++ wrapper that provides a simple and unified interface to
// several linear programming and mixed integer programming solvers:
// GLPK, CLP, CBC and SCIP. The wrapper can also be used in Java and
// Python via SWIG.
//
//
// -----------------------------------
// What is Linear Programming?
//
//   In mathematics, linear programming (LP) is a technique for optimization of
//   a linear objective function, subject to linear equality and linear
//   inequality constraints. Informally, linear programming determines the way
//   to achieve the best outcome (such as maximum profit or lowest cost) in a
//   given mathematical model and given some list of requirements represented
//   as linear equations.
//
//   The most widely used technique for solving a linear program is the Simplex
//   algorithm, devised by George Dantzig in 1947. It performs very well on
//   most instances, for which its running time is polynomial. A lot of effort
//   has been put into improving the algorithm and its implementation. As a
//   byproduct, it has however been shown that one can always construct
//   problems that take exponential time for the Simplex algorithm to solve.
//   Research has thus focused on trying to find a polynomial algorithm for
//   linear programming, or to prove that linear programming is indeed
//   polynomial.
//
//   Leonid Khachiyan first exhibited in 1979 a weakly polynomial algorithm for
//   linear programming. "Weakly polynomial" means that the running time of the
//   algorithm is in O(P(n) * 2^p) where P(n) is a polynomial of the size of the
//   problem, and p is the precision of computations expressed in number of
//   bits. With a fixed-precision, floating-point-based implementation, a weakly
//   polynomial algorithm will  thus run in polynomial time. No implementation
//   of Khachiyan's algorithm has proved efficient, but a larger breakthrough in
//   the field came in 1984 when Narendra Karmarkar introduced a new interior
//   point method for solving linear programming problems. Interior point
//   algorithms have proved efficient on very large linear programs.
//
//   Check Wikipedia for more detail:
//     http://en.wikipedia.org/wiki/Linear_programming
//
// -----------------------------------
// Example of a Linear Program
//
//   maximize:
//     3x + y
//   subject to:
//     1.5 x + 2 y <= 12
//     0 <= x <= 3
//     0 <= y <= 5
//
//  A linear program has:
//    1) a linear objective function
//    2) linear constraints that can be equalities or inequalities
//    3) bounds on variables that can be positive, negative, finite or
//       infinite.
//
// -----------------------------------
// What is Mixed Integer Programming?
//
//   Here, the constraints and the objective are still linear but
//   there are additional integrality requirements for variables. If
//   all variables are required to take integer values, then the
//   problem is called an integer program (IP). In most cases, only
//   some variables are required to be integer and the rest of the
//   variables are continuous: this is called a mixed integer program
//   (MIP). IPs and MIPs are generally NP-hard.
//
//   Integer variables can be used to model discrete decisions (build a
//   datacenter in city A or city B), logical relationships (only
//   place machines in datacenter A if we have decided to build
//   datacenter A) and approximate non-linear functions with piecewise
//   linear functions (for example, the cost of machines as a function
//   of how many machines are bought, or the latency of a server as a
//   function of its load).
//
// -----------------------------------
// How to use the wrapper?
//
//   The user builds the model and solves it through the MPSolver class,
//   then queries the solution through the MPSolver, MPVariable and
//   MPConstraint classes. To be able to query a solution, you need the
//   following:
//   - A solution exists: MPSolver::Solve has been called and a solution
//     has been found.
//   - The model has not been modified since the last time
//     MPSolver::Solve was called. Otherwise, the solution obtained
//     before the model modification may not longer be feasible or
//     optimal.
//
//   @see ../examples/linear_programming.cc for a simple LP example
//   @see ../examples/integer_programming.cc for a simple MIP example
//
//   All methods cannot be called successfully in all cases. For
//   example: you cannot query a solution when no solution exists, you
//   cannot query a reduced cost value (which makes sense only on
//   continuous problems) on a discrete problem. When a method is
//   called in an unsuitable context, it aborts with a
//   LOG(FATAL).
// TODO(user): handle failures gracefully.
//
// -----------------------------------
// For developers: How the does the wrapper work?
//
//   MPSolver stores a representation of the model (variables,
//   constraints and objective) in its own data structures and a
//   pointer to a MPSolverInterface that wraps the underlying solver
//   (CBC, CLP, GLPK or SCIP) that does the actual work. The
//   underlying solver also keeps a representation of the model in its
//   own data structures. The model representations in MPSolver and in
//   the underlying solver are kept in sync by the 'extraction'
//   mechanism: synchronously for some changes and asynchronously
//   (when MPSolver::Solve is called) for others. Synchronicity
//   depends on the modification applied and on the underlying solver.

#ifndef OR_TOOLS_LINEAR_SOLVER_LINEAR_SOLVER_H_
#define OR_TOOLS_LINEAR_SOLVER_LINEAR_SOLVER_H_

#include "base/hash.h"
#include "base/hash.h"
#include <limits>
#include <string>
#include <vector>

#include "base/commandlineflags.h"
#include "base/integral_types.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/scoped_ptr.h"
#include "base/timer.h"
#include "base/strutil.h"
#include "base/sparsetable.h"
#include "base/hash.h"

using std::string;

namespace operations_research {

class MPConstraint;
class MPModelProto;
class MPModelRequest;
class MPObjective;
class MPSolutionResponse;
class MPSolverInterface;
class MPSolverParameters;
class MPVariable;

// This mathematical programming (MP) solver class is the main class
// though which users build and solve problems.
class MPSolver {
 public:
  // The type of problems (LP or MIP) that will be solved and the
  // underlying solver (GLPK, CLP, CBC or SCIP) that will solve them.
  enum OptimizationProblemType {
#if defined(USE_GLPK)
    GLPK_LINEAR_PROGRAMMING,
    GLPK_MIXED_INTEGER_PROGRAMMING,
#endif
#if defined(USE_CLP)
    CLP_LINEAR_PROGRAMMING,
#endif
#if defined(USE_CBC)
    CBC_MIXED_INTEGER_PROGRAMMING,
#endif
#if defined(USE_SCIP)
    SCIP_MIXED_INTEGER_PROGRAMMING,
#endif
  };

  // The status of solving the problem.
  // TODO(user): Figure out once and for all what the status of
  // underlying solvers exactly mean, especially for feasible and
  // infeasible.
  enum ResultStatus {
    OPTIMAL,     // optimal.
    FEASIBLE,    // feasible, or stopped by limit.
    INFEASIBLE,  // proven infeasible.
    UNBOUNDED,   // proven unbounded.
    ABNORMAL,    // abnormal, i.e., error of some kind.
    NOT_SOLVED   // not been solved yet.
  };

  // The status of loading the problem from a protocol buffer.
  enum LoadStatus {
    NO_ERROR,               // no error has been encountered.
    DUPLICATE_VARIABLE_ID,  // error: two variables have the same id.
    UNKNOWN_VARIABLE_ID     // error: a variable has an unknown id.
  };

  // Advanced usage: possible basis status values for a variable and the
  // slack variable of a linear constraint.
  enum BasisStatus {
    FREE = 0,
    AT_LOWER_BOUND,
    AT_UPPER_BOUND,
    FIXED_VALUE,
    BASIC
  };

  MPSolver(const string& name, OptimizationProblemType problem_type);
  virtual ~MPSolver();

  // ----- Init and Clear -----
  // TODO(user): Find out what this does and possibly remove.
  void Init() {}
  void Clear();

  // ----- Variables ------
  // Returns the number of variables.
  int NumVariables() const { return variables_.size(); }

  // Creates a variable with the given bounds, integrality requirement
  // and name. Bounds can be finite or +/- MPSolver::infinity().
  MPVariable* MakeVar(double lb, double ub, bool integer, const string& name);
  // Creates a continuous variable.
  MPVariable* MakeNumVar(double lb, double ub, const string& name);
  // Creates an integer variable.
  MPVariable* MakeIntVar(double lb, double ub, const string& name);
  // Creates a boolean variable.
  MPVariable* MakeBoolVar(const string& name);

  // Creates an array of variables. All variables created have the
  // same bounds and integrality requirement.
  // @param name the prefix of the variable names. Variables are named
  // name0, name1, ...
  void MakeVarArray(int nb,
                    double lb,
                    double ub,
                    bool integer,
                    const string& name,
                    std::vector<MPVariable*>* vars);
  // Creates an array of continuous variables.
  void MakeNumVarArray(int nb,
                       double lb,
                       double ub,
                       const string& name,
                       std::vector<MPVariable*>* vars);
  // Creates an array of integer variables.
  void MakeIntVarArray(int nb,
                       double lb,
                       double ub,
                       const string& name,
                       std::vector<MPVariable*>* vars);
  // Creates an array of boolean variables.
  void MakeBoolVarArray(int nb,
                        const string& name,
                        std::vector<MPVariable*>* vars);

  // ----- Constraints -----
  // Returns the number of constraints.
  int NumConstraints() const { return constraints_.size(); }

  // Creates a linear constraint with given bounds. Bounds can be
  // finite or +/- MPSolver::infinity(). The MPSolver class assumes
  // ownership of the constraint.
  // @return a pointer to the newly created constraint.
  MPConstraint* MakeRowConstraint(double lb, double ub);
  // Creates a constraint with -infinity and +infinity bounds.
  MPConstraint* MakeRowConstraint();
  // Creates a named constraint with given bounds.
  MPConstraint* MakeRowConstraint(double lb, double ub, const string& name);
  // Creates a named constraint with -infinity and +infinity bounds.
  MPConstraint* MakeRowConstraint(const string& name);

  // ----- Objective -----

  // Returns the objective value of the best solution found so far. It
  // is the optimal objective value if the problem has been solved to
  // optimality.
  double objective_value() const;

  // Returns the best objective bound. In case of minimization, it is
  // a lower bound on the objective value of the optimal integer
  // solution. Only available for discrete problems.
  double best_objective_bound() const;

  // Clears the objective: the objective coefficient of each variable
  // is 0 and the objective offset is 0.
  void ClearObjective();
  // Adds var * coeff to the objective.
  void AddObjectiveTerm(MPVariable* const var, double coeff);
  // Adds var (with coefficient 1) to the objective.
  void AddObjectiveTerm(MPVariable* const var);
  // Sets the objective coefficient of a variable in the objective.
  void SetObjectiveCoefficient(MPVariable* const var, double coeff);
  // Adds a constant term to the objective.
  void AddObjectiveOffset(double value);
  // Sets the constant term in the objective.
  void SetObjectiveOffset(double value);

  // Sets the optimization direction (maximize: true or minimize: false).
  void SetOptimizationDirection(bool maximize);
  // Sets the optimization direction to minimize.
  void SetMinimization() { SetOptimizationDirection(false); }
  // Sets the optimization direction to maximize.
  void SetMaximization() { SetOptimizationDirection(true); }
  // Is the optimization direction set to maximize?
  bool Maximization() const;
  // Is the optimization direction set to minimize?
  bool Minimization() const;

  // ----- Solve -----
  // Solves the problem using default parameter values.
  ResultStatus Solve();
  // Solves the problem using the specified parameter values.
  ResultStatus Solve(const MPSolverParameters& param);

  // Advanced usage: resets extracted model to solve from scratch.
  void Reset();

  // ----- Methods using protocol buffers -----

  // Loads model from protocol buffer.
  LoadStatus LoadModel(const MPModelProto& model);

  // Exports model to protocol buffer.
  void ExportModel(MPModelProto* model) const;

  // Encodes the current solution in a solution response protocol buffer.
  // Only nonzero variable values are stored in order to reduce the
  // size of the MPSolutionResponse protocol buffer.
  void FillSolutionResponse(MPSolutionResponse* response) const;

  // Solves the model encoded by a MPModelRequest protocol buffer and
  // fills the solution encoded as a MPSolutionResponse.
  // The model is solved by the interface specified in the constructor
  // of MPSolver, MPModelRequest.OptimizationProblemType is ignored.
  void SolveWithProtocolBuffers(const MPModelRequest& model_request,
                                MPSolutionResponse* response);

  // ----- Misc -----

  // Infinity. You can use -MPSolver::infinity() for negative infinity.
  static double infinity() {
    return std::numeric_limits<double>::infinity();
  }

  // Suppresses all output from the underlying solver.
  void SuppressOutput();

  // Enables a reasonably verbose output from the underlying
  // solver. The level of verbosity and the location of this output
  // depends on the underlying solver. In most cases, it is sent to
  // stdout.
  void EnableOutput();

  void set_write_model_filename(const string &filename) {
    write_model_filename_ = filename;
  }

  string write_model_filename() const {
    return write_model_filename_;
  }

  void set_time_limit(int64 time_limit) {
    DCHECK_GE(time_limit, 0);
    time_limit_ = time_limit;
  }

  int64 time_limit() const {
    return time_limit_;
  }

  // Returns wall_time() in milliseconds since the creation of the solver.
  int64 wall_time() const {
    return timer_.GetInMs();
  }

  // Returns the number of simplex iterations.
  int64 iterations() const;

  // Returns the number of branch-and-bound nodes. Only available for
  // discrete problems.
  int64 nodes() const;

  // Checks the validity of a variable or constraint name.
  bool CheckNameValidity(const string& name);
  // Checks the validity of all variables and constraints names.
  bool CheckAllNamesValidity();

  // Returns a string describing the underlying solver and its version.
  string SolverVersion() const;

  // Advanced usage: returns the underlying solver so that the user
  // can use solver-specific features or features that are not exposed
  // in the simple API of MPSolver. This method is for advanced users,
  // use at your own risk! In particular, if you modify the model or
  // the solution by accessing the underlying solver directly, then
  // the underlying solver will be out of sync with the information
  // kept in the wrapper (MPSolver, MPVariable, MPConstraint,
  // MPObjective). You need to cast the void* returned back to its
  // original type that depends on the interface (CBC:
  // OsiClpSolverInterface*, CLP: ClpSimplex*, GLPK: glp_prob*, SCIP:
  // SCIP*).
  void* underlying_solver();

  // Advanced usage: computes the exact condition number of the
  // current scaled basis: L1norm(B) * L1norm(inverse(B)), where B is
  // the scaled basis.
  // This method requires that a basis exists: it should be called
  // after Solve. It is only available for continuous problems. It is
  // implemented for GLPK but not CLP because CLP does not provide the
  // API for doing it.
  // The condition number measures how well the constraint matrix is
  // conditioned and can be used to predict whether numerical issues
  // will arise during the solve: the model is declared infeasible
  // whereas it is feasible (or vice-versa), the solution obtained is
  // not optimal or violates some constraints, the resolution is slow
  // because of repeated singularities.
  // The rule of thumb to interpret the condition number kappa is:
  // o kappa <= 1e7: virtually no chance of numerical issues
  // o 1e7 < kappa <= 1e10: small chance of numerical issues
  // o 1e10 < kappa <= 1e13: medium chance of numerical issues
  // o kappa > 1e13: high chance of numerical issues
  // The computation of the condition number depends on the quality of
  // the LU decomposition, so it is not very accurate when the matrix
  // is ill conditioned.
  double ComputeExactConditionNumber() const;

  friend class GLPKInterface;
  friend class CLPInterface;
  friend class CBCInterface;
  friend class SCIPInterface;
  friend class MPSolverInterface;

 private:
  // Computes the size of the constraint with the largest number of
  // coefficients with index in [min_constraint_index,
  // max_constraint_index)
  int ComputeMaxConstraintSize(int min_constraint_index,
                               int max_constraint_index) const;

  // Returns true if the model has constraints with lower bound > upper bound.
  bool HasInfeasibleConstraints() const;

  // The name of the linear programming problem.
  const string name_;

  // The solver interface.
  scoped_ptr<MPSolverInterface> interface_;

  // The vector of variables in the problem.
  std::vector<MPVariable*> variables_;
  hash_set<string> variables_names_;

  // The vector of constraints in the problem.
  std::vector<MPConstraint*> constraints_;
  hash_set<string> constraints_names_;

  // The linear objective function.
  scoped_ptr<MPObjective> linear_objective_;

  // Time limit in milliseconds (0 = no limit).
  int64 time_limit_;

  // Name of the file where the solver writes out the model when Solve
  // is called. If empty, no file is written.
  string write_model_filename_;

  WallTimer timer_;

  DISALLOW_COPY_AND_ASSIGN(MPSolver);
};

// The class for variables of a Mathematical Programming (MP) model.
class MPVariable {
 public:
  // Returns the name of the variable.
  const string& name() const { return name_; }

  // Sets the integrality requirement of the variable.
  void SetInteger(bool integer);
  // Returns the integrality requirement of the variable.
  bool integer() const { return integer_; }

  // Returns the value of the variable in the current solution.
  double solution_value() const;

  // Returns the index of the variable in the MPSolver::variables_.
  int index() const { return index_; }

  // Returns the lower bound.
  double lb() const { return lb_; }
  // Returns the upper bound.
  double ub() const { return ub_; }
  // Sets the lower bound.
  void SetLB(double lb) { SetBounds(lb, ub_); }
  // Sets the upper bound.
  void SetUB(double ub) { SetBounds(lb_, ub); }
  // Sets both the lower and upper bounds.
  void SetBounds(double lb, double ub);

  // Advanced usage: returns the reduced cost of the variable in the
  // current solution (only available for continuous problems).
  double reduced_cost() const;
  // Advanced usage: returns the basis status of the variable in the
  // current solution (only available for continuous problems).
  // @see MPSolver::BasisStatus.
  MPSolver::BasisStatus basis_status() const;

 protected:
  friend class MPSolver;
  friend class MPSolverInterface;
  friend class CBCInterface;
  friend class CLPInterface;
  friend class GLPKInterface;
  friend class SCIPInterface;

  // Constructor. A variable points to a single MPSolverInterface that
  // is specified in the constructor. A variable cannot belong to
  // several models.
  MPVariable(double lb, double ub, bool integer, const string& name,
             MPSolverInterface* const interface)
      : lb_(lb), ub_(ub), integer_(integer), name_(name), index_(-1),
        solution_value_(0.0), reduced_cost_(0.0), interface_(interface) {}

  void set_index(int index) { index_ = index; }
  void set_solution_value(double value) { solution_value_ = value; }
  void set_reduced_cost(double reduced_cost) { reduced_cost_ = reduced_cost; }

 private:
  double lb_;
  double ub_;
  bool integer_;
  const string name_;
  int index_;
  double solution_value_;
  double reduced_cost_;
  MPSolverInterface* const interface_;
  DISALLOW_COPY_AND_ASSIGN(MPVariable);
};

// The class for constraints of a Mathematical Programming (MP) model.
// A constraint is represented as a linear equation or inequality.
class MPConstraint {
 public:
  // Returns the name of the constraint.
  const string& name() const { return name_; }

  // Clears all variables and coefficients. Does not clear the bounds.
  void Clear();

  // Adds var * coeff to the constraint.
  void AddTerm(MPVariable* const var, double coeff);
  // Adds var (with coefficient 1) to the constraint.
  void AddTerm(MPVariable* const var);
  // Sets the coefficient of the variable on the constraint.
  void SetCoefficient(MPVariable* const var, double coeff);

  // Returns the lower bound.
  double lb() const { return lb_; }
  // Returns the upper bound.
  double ub() const { return ub_; }
  // Sets the lower bound.
  void SetLB(double lb) { SetBounds(lb, ub_); }
  // Sets the upper bound.
  void SetUB(double ub) { SetBounds(lb_, ub); }
  // Sets both the lower and upper bounds.
  void SetBounds(double lb, double ub);

  // Returns the constraint's activity in the current solution:
  // sum over all terms of (coefficient * variable value)
  double activity() const;

  // Returns the index of the constraint in the MPSolver::constraints_.
  // TODO(user): move to protected.
  int index() const { return index_; }

  // Advanced usage: returns the dual value of the constraint in the
  // current solution (only available for continuous problems).
  double dual_value() const;
  // Advanced usage: returns the basis status of the slack variable
  // associated with the constraint (only available for continuous
  // problems).
  // @see MPSolver::BasisStatus.
  MPSolver::BasisStatus basis_status() const;

 protected:
  friend class MPSolver;
  friend class MPSolverInterface;
  friend class CBCInterface;
  friend class CLPInterface;
  friend class GLPKInterface;
  friend class SCIPInterface;

  // Constructor. A constraint points to a single MPSolverInterface
  // that is specified in the constructor. A constraint cannot belong
  // to several models.
  MPConstraint(double lb,
               double ub,
               const string& name,
               MPSolverInterface* const interface)
      : lb_(lb), ub_(ub), name_(name), index_(-1), dual_value_(0.0),
        activity_(0.0), interface_(interface) {}

  void set_index(int index) { index_ = index; }
  void set_activity(double activity) { activity_ = activity; }
  void set_dual_value(double dual_value) { dual_value_ = dual_value; }

 private:
  // Returns true if the constraint contains variables that have not
  // been extracted yet.
  bool ContainsNewVariables();

  // Mapping var -> coefficient.
  hash_map<MPVariable*, double> coefficients_;

  // The lower bound for the linear constraint.
  double lb_;
  // The upper bound for the linear constraint.
  double ub_;
  // Name.
  const string name_;
  int index_;
  double dual_value_;
  double activity_;
  MPSolverInterface* const interface_;
  DISALLOW_COPY_AND_ASSIGN(MPConstraint);
};


// A class to express a linear objective.
class MPObjective {
 public:
  // Clears all variables and coefficients.
  void Clear();

  // Adds var * coeff to the objective.
  void AddTerm(MPVariable* const var, double coeff);
  // Adds variable (with coefficient 1) to the objective.
  void AddTerm(MPVariable* const var);
  // Sets the coefficient of the variable in the objective.
  void SetCoefficient(MPVariable* const var, double coeff);

  // Adds a constant term to the objective.
  void AddOffset(double value);
  // Sets the constant term in the objective.
  void SetOffset(double value);

 private:
  friend class MPSolver;
  friend class MPSolverInterface;
  friend class CBCInterface;
  friend class CLPInterface;
  friend class GLPKInterface;
  friend class SCIPInterface;

  // Constructor. An objective points to a single MPSolverInterface
  // that is specified in the constructor. An objective cannot belong
  // to several models.
  explicit MPObjective(MPSolverInterface* const interface)
      : offset_(0.0), interface_(interface) {}

  // Mapping var -> coefficient.
  hash_map<MPVariable*, double> coefficients_;
  // Constant term.
  double offset_;

  MPSolverInterface* const interface_;
  DISALLOW_COPY_AND_ASSIGN(MPObjective);
};



// This class stores parameter settings for LP and MIP solvers.
// Some parameters are marked as advanced: do not change their values
// unless you know what you are doing!
//
// For developers: how to add a new parameter:
// - Add the new Foo parameter in the DoubleParam or IntegerParam enum.
// - If it is a categorical param, add a FooValues enum.
// - Decide if the wrapper should define a default value for it: yes
//   if it controls the properties of the solution (example:
//   tolerances) or if it consistently improves performance, no
//   otherwise. If yes, define kDefaultFoo.
// - Add a foo_value_ member and, if no default value is defined, a
//   foo_is_default_ member.
// - Add code to handle Foo in Set...Param, Reset...Param,
//   Get...Param, Reset and the constructor.
// - In class MPSolverInterface, add a virtual method SetFoo, add it
//   to SetCommonParameters or SetMIPParameters, and implement it for
//   each solver. Sometimes, parameters need to be implemented
//   differently, see for example the INCREMENTALITY implementation.
// - Add a test in linear_solver_test.cc.
//
// TODO(user): store the parameter values in a protocol buffer
// instead. We need to figure out how to deal with the subtleties of
// the default values.
class MPSolverParameters {
 public:
  // Enumeration of parameters that take continuous values.
  enum DoubleParam {
    // Limit for relative MIP gap.
    RELATIVE_MIP_GAP = 0,
    // Advanced usage: tolerance for primal feasibility of basic
    // solutions. This does not control the integer feasibility
    // tolerance of integer solutions for MIP or the tolerance used
    // during presolve.
    PRIMAL_TOLERANCE = 1,
    // Advanced usage: tolerance for dual feasibility of basic solutions.
    DUAL_TOLERANCE = 2
  };

  // Enumeration of parameters that take integer or categorical values.
  enum IntegerParam {
    // Advanced usage: presolve mode.
    PRESOLVE = 1000,
    // Algorithm to solve linear programs.
    LP_ALGORITHM = 1001,
    // Advanced usage: incrementality from one solve to the next.
    INCREMENTALITY = 1002
  };

  // For each categorical parameter, enumeration of possible values.
  enum PresolveValues {
    PRESOLVE_OFF = 0,  // Presolve is off.
    PRESOLVE_ON = 1    // Presolve is on.
  };

  enum LpAlgorithmValues {
    DUAL = 10,      // Dual simplex.
    PRIMAL = 11,    // Primal simplex.
    BARRIER = 12    // Barrier algorithm.
  };

  enum IncrementalityValues {
    // Start solve from scratch.
    INCREMENTALITY_OFF = 0,
    // Reuse results from previous solve as much as the underlying
    // solver allows.
    INCREMENTALITY_ON = 1
  };

  // @{
  // Placeholder value to indicate that a parameter is set to
  // the default value defined in the wrapper.
  static const double kDefaultDoubleParamValue;
  static const int kDefaultIntegerParamValue;
  // @}

  // @{
  // Placeholder value to indicate that a parameter is unknown.
  static const double kUnknownDoubleParamValue;
  static const int kUnknownIntegerParamValue;
  // @}

  // @{
  // Default values for parameters. Only parameters that define the
  // properties of the solution returned need to have a default value
  // (that is the same for all solvers). You can also define a default
  // value for performance parameters when you are confident it is a
  // good choice (example: always turn presolve on).
  static const double kDefaultRelativeMipGap;
  static const double kDefaultPrimalTolerance;
  static const double kDefaultDualTolerance;
  static const PresolveValues kDefaultPresolve;
  static const IncrementalityValues kDefaultIncrementality;
  // @}

  // The constructor sets all parameters to their default value.
  MPSolverParameters();

  // @{
  // Sets a parameter to a specific value.
  void SetDoubleParam(MPSolverParameters::DoubleParam param, double value);
  void SetIntegerParam(MPSolverParameters::IntegerParam param, int value);
  // @}

  // @{
  // Sets a parameter to its default value (default value defined
  // in MPSolverParameters if it exists, otherwise the default value
  // defined in the underlying solver).
  void ResetDoubleParam(MPSolverParameters::DoubleParam param);
  void ResetIntegerParam(MPSolverParameters::IntegerParam param);
  // Sets all parameters to their default value.
  void Reset();
  // @}

  // @{
  // Returns the value of a parameter.
  double GetDoubleParam(MPSolverParameters::DoubleParam param) const;
  int GetIntegerParam(MPSolverParameters::IntegerParam param) const;
  // @}

 private:
  // @{
  // Parameter value for each parameter.
  // @see DoubleParam
  // @see IntegerParam
  double relative_mip_gap_value_;
  double primal_tolerance_value_;
  double dual_tolerance_value_;
  int presolve_value_;
  int lp_algorithm_value_;
  int incrementality_value_;
  // @}

  // Boolean value indicating whether each parameter is set to the
  // solver's default value. Only parameters for which the wrapper
  // does not define a default value need such an indicator.
  bool lp_algorithm_is_default_;

  DISALLOW_COPY_AND_ASSIGN(MPSolverParameters);
};


// This class wraps the actual mathematical programming solvers. Each
// solver (CLP, CBC, GLPK, SCIP) has its own interface class that
// derives from this abstract class. This class is never directly
// accessed by the user.
// @see cbc_interface.cc
// @see clp_interface.cc
// @see glpk_interface.cc
// @see scip_interface.cc
class MPSolverInterface {
 public:
  enum SynchronizationStatus {
    // The underlying solver (CLP, GLPK, ...) and MPSolver are not in
    // sync for the model nor for the solution.
    MUST_RELOAD,
    // The underlying solver and MPSolver are in sync for the model
    // but not for the solution: the model has changed since the
    // solution was computed last.
    MODEL_SYNCHRONIZED,
    // The underlying solver and MPSolver are in sync for the model and
    // the solution.
    SOLUTION_SYNCHRONIZED
  };

  // When the underlying solver does not provide the number of simplex
  // iterations.
  static const int64 kUnknownNumberOfIterations = -1;
  // When the underlying solver does not provide the number of
  // branch-and-bound nodes.
  static const int64 kUnknownNumberOfNodes = -1;
  // When the index of a variable or constraint has not been assigned yet.
  static const int kNoIndex = -1;

  // Constructor. The user will access the MPSolverInterface through the
  // MPSolver passed as argument.
  explicit MPSolverInterface(MPSolver* const solver);
  virtual ~MPSolverInterface();

  // ----- Solve -----
  // Solves problem with specified parameter values. Returns true if the
  // solution is optimal. Calls WriteModelToPredefinedFiles
  // to allow the user to write the model to a file.
  virtual MPSolver::ResultStatus Solve(const MPSolverParameters& param) = 0;

  // ----- Model modifications and extraction -----
  // Resets extracted model.
  virtual void Reset() = 0;

  // Sets the optimization direction (min/max).
  virtual void SetOptimizationDirection(bool maximize) = 0;

  // Modifies bounds of an extracted variable.
  virtual void SetVariableBounds(int index, double lb, double ub) = 0;

  // Modifies integrality of an extracted variable.
  virtual void SetVariableInteger(int index, bool integer) = 0;

  // Modify bounds of an extracted variable.
  virtual void SetConstraintBounds(int index, double lb, double ub) = 0;

  // Adds a linear constraint.
  virtual void AddRowConstraint(MPConstraint* const ct) = 0;

  // Add a variable.
  virtual void AddVariable(MPVariable* const var) = 0;

  // Changes a coefficient in a constraint.
  virtual void SetCoefficient(MPConstraint* const constraint,
                              MPVariable* const variable,
                              double new_value,
                              double old_value) = 0;

  // Clears a constraint from all its terms.
  virtual void ClearConstraint(MPConstraint* const constraint) = 0;

  // Changes a coefficient in the linear objective.
  virtual void SetObjectiveCoefficient(MPVariable* const variable,
                                       double coefficient) = 0;

  // Changes the constant term in the linear objective.
  virtual void SetObjectiveOffset(double value) = 0;

  // Clears the objective from all its terms.
  virtual void ClearObjective() = 0;

  // ------ Query statistics on the solution and the solve ------
  // Returns the number of simplex iterations.
  virtual int64 iterations() const = 0;
  // Returns the number of branch-and-bound nodes
  virtual int64 nodes() const = 0;
  // Returns the best objective bound. Only available for discrete problems.
  virtual double best_objective_bound() const = 0;
  // Returns the objective value of the best solution found so far.
  double objective_value() const;

  // Returns the basis status of a row.
  virtual MPSolver::BasisStatus row_status(int constraint_index) const = 0;
  // Returns the basis status of a constraint.
  virtual MPSolver::BasisStatus column_status(int variable_index) const = 0;

  // Checks whether the solution is synchronized with the model,
  // i.e. whether the model has changed since the solution was
  // computed last.
  void CheckSolutionIsSynchronized() const;
  // Checks whether a feasible solution exists.
  virtual void CheckSolutionExists() const;
  // Checks whether information on the best objective bound exists.
  virtual void CheckBestObjectiveBoundExists() const;

  // ----- Misc -----
  // Writes model to a file.
  virtual void WriteModel(const string& filename) = 0;

  // Queries problem type. For simplicity, the distinction between
  // continuous and discrete is based on the declaration of the user
  // when the solver is created (example: GLPK_LINEAR_PROGRAMMING
  // vs. GLPK_MIXED_INTEGER_PROGRAMMING), not on the actual content of
  // the model.
  // Returns true if the problem is continuous.
  virtual bool IsContinuous() const = 0;
  // Returns true if the problem is continuous and linear.
  virtual bool IsLP() const = 0;
  // Returns true if the problem is discrete and linear.
  virtual bool IsMIP() const = 0;

  // Returns the index of the last variable extracted.
  int last_variable_index() const {
    return last_variable_index_;
  }

  // Returns the boolean indicating the verbosity of the solver output.
  bool quiet() const {
    return quiet_;
  }
  // Sets the boolean indicating the verbosity of the solver output.
  void set_quiet(bool quiet_value) {
    quiet_ = quiet_value;
  }

  // Returns the result status of the last solve.
  MPSolver::ResultStatus result_status() const {
    CheckSolutionIsSynchronized();
    return result_status_;
  }

  // Returns a string describing the underlying solver and its version.
  virtual string SolverVersion() const = 0;

  // Returns the underlying solver.
  virtual void* underlying_solver() = 0;

  // Computes exact condition number. Only available for continuous
  // problems and only implemented in GLPK.
  virtual double ComputeExactConditionNumber() const = 0;

  friend class MPSolver;
 protected:
  MPSolver* const solver_;
  // Indicates whether the model and the solution are synchronized.
  SynchronizationStatus sync_status_;
  // Indicates whether the solve has reached optimality,
  // infeasibility, a limit, etc.
  MPSolver::ResultStatus result_status_;
  // Optimization direction.
  bool maximize_;

  // Index in MPSolver::variables_ of last constraint extracted.
  int last_constraint_index_;
  // Index in MPSolver::constraints_ of last variable extracted.
  int last_variable_index_;

  // The value of the objective function.
  double objective_value_;

  // Boolean indicator for the verbosity of the solver output.
  bool quiet_;

  // Index of dummy variable created for empty constraints or the
  // objective offset.
  static const int kDummyVariableIndex;

  // Writes out the model to a file specified by the
  // --solver_write_model command line argument or
  // MPSolver::set_write_model_filename.
  // The file is written by each solver interface (CBC, CLP, GLPK, SCIP) and
  // each behaves a little differently.
  // If filename ends in ".lp", then the file is written in the
  // LP format (except for the CLP solver that does not support the LP
  // format). In all other cases it is written in the MPS format.
  void WriteModelToPredefinedFiles();

  // Extracts model stored in MPSolver.
  void ExtractModel();
  // Extracts the variables that have not been extracted yet.
  virtual void ExtractNewVariables() = 0;
  // Extracts the constraints that have not been extracted yet.
  virtual void ExtractNewConstraints() = 0;
  // Extracts the objective.
  virtual void ExtractObjective() = 0;
  // Resets the extraction information.
  void ResetExtractionInformation();
  // Change synchronization status from SOLUTION_SYNCHRONIZED to
  // MODEL_SYNCHRONIZED. To be used for model changes.
  void InvalidateSolutionSynchronization();

  // Sets parameters common to LP and MIP in the underlying solver.
  void SetCommonParameters(const MPSolverParameters& param);
  // Sets MIP specific parameters in the underlying solver.
  void SetMIPParameters(const MPSolverParameters& param);
  // Sets all parameters in the underlying solver.
  virtual void SetParameters(const MPSolverParameters& param) = 0;
  // Sets an unsupported double parameter.
  void SetUnsupportedDoubleParam(MPSolverParameters::DoubleParam param) const;
  // Sets an unsupported integer parameter.
  void SetUnsupportedIntegerParam(MPSolverParameters::IntegerParam param) const;
  // Sets a supported double parameter to an unsupported value.
  void SetDoubleParamToUnsupportedValue(MPSolverParameters::DoubleParam param,
                                        int value) const;
  // Sets a supported integer parameter to an unsupported value.
  void SetIntegerParamToUnsupportedValue(MPSolverParameters::IntegerParam param,
                                        double value) const;
  // Sets each parameter in the underlying solver.
  virtual void SetRelativeMipGap(double value) = 0;
  virtual void SetPrimalTolerance(double value) = 0;
  virtual void SetDualTolerance(double value) = 0;
  virtual void SetPresolveMode(int value) = 0;
  virtual void SetLpAlgorithm(int value) = 0;
};

}  // namespace operations_research

#endif  // OR_TOOLS_LINEAR_SOLVER_LINEAR_SOLVER_H_
