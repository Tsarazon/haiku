/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Private implementation of Cassowary constraint solver.
 * This header is internal to Spektr Kit and should not be included directly.
 */
#ifndef _KOSM_CASSOWARY_HPP
#define _KOSM_CASSOWARY_HPP

#include <cstdint>
#include <cmath>

namespace KosmOS {
namespace Cassowary {

static constexpr uint32_t MAX_VARS = 256;
static constexpr uint32_t MAX_CONSTRAINTS = 512;
static constexpr uint32_t MAX_ROWS = MAX_CONSTRAINTS + MAX_VARS;
static constexpr uint32_t MAX_EXPR_TERMS = 32;
static constexpr float EPSILON = 1e-6f;

static constexpr float Required = 1001.0f;
static constexpr float Strong = 1000.0f;
static constexpr float Medium = 500.0f;
static constexpr float Weak = 1.0f;

enum class SymbolType : uint8_t {
	Invalid,
	External,
	Slack,
	Error,
	Dummy
};

struct Symbol {
	uint32_t	id;
	SymbolType	type;

	bool operator==(const Symbol& other) const {
		return id == other.id && type == other.type;
	}

	bool operator!=(const Symbol& other) const {
		return !(*this == other);
	}

	bool IsInvalid() const { return type == SymbolType::Invalid; }
	bool IsExternal() const { return type == SymbolType::External; }
	bool IsSlack() const { return type == SymbolType::Slack; }
	bool IsError() const { return type == SymbolType::Error; }
	bool IsDummy() const { return type == SymbolType::Dummy; }
	bool IsPivotable() const {
		return type == SymbolType::Slack || type == SymbolType::Error;
	}
	bool IsRestricted() const { return type != SymbolType::External; }
};

static constexpr Symbol InvalidSymbol = {0, SymbolType::Invalid};

struct Row {
	float		constant;
	Symbol		symbols[MAX_VARS];
	float		coeffs[MAX_VARS];
	uint32_t	term_count;

	Row() : constant(0), term_count(0) {}

	void Clear() {
		constant = 0;
		term_count = 0;
	}

	float CoefficientFor(Symbol sym) const {
		for (uint32_t i = 0; i < term_count; ++i) {
			if (symbols[i] == sym) return coeffs[i];
		}
		return 0;
	}

	void SetCoefficient(Symbol sym, float value) {
		for (uint32_t i = 0; i < term_count; ++i) {
			if (symbols[i] == sym) {
				if (fabsf(value) < EPSILON) {
					symbols[i] = symbols[term_count - 1];
					coeffs[i] = coeffs[term_count - 1];
					--term_count;
				} else {
					coeffs[i] = value;
				}
				return;
			}
		}

		if (fabsf(value) >= EPSILON && term_count < MAX_VARS) {
			symbols[term_count] = sym;
			coeffs[term_count] = value;
			++term_count;
		}
	}

	void AddCoefficient(Symbol sym, float delta) {
		SetCoefficient(sym, CoefficientFor(sym) + delta);
	}

	void AddRow(const Row& other, float scale) {
		constant += other.constant * scale;
		for (uint32_t i = 0; i < other.term_count; ++i) {
			AddCoefficient(other.symbols[i], other.coeffs[i] * scale);
		}
	}

	void SolveFor(Symbol sym) {
		float coeff = CoefficientFor(sym);
		if (fabsf(coeff) < EPSILON) return;

		float scale = -1.0f / coeff;
		constant *= scale;

		SetCoefficient(sym, 0);

		for (uint32_t i = 0; i < term_count; ++i) {
			coeffs[i] *= scale;
		}
	}

	void SolveFor(Symbol lhs, Symbol rhs) {
		AddCoefficient(lhs, -1.0f);
		SolveFor(rhs);
	}

	void Substitute(Symbol sym, const Row& row) {
		float coeff = CoefficientFor(sym);
		if (fabsf(coeff) < EPSILON) return;

		SetCoefficient(sym, 0);
		AddRow(row, coeff);
	}

	Symbol AnyPivotableSymbol() const {
		for (uint32_t i = 0; i < term_count; ++i) {
			if (symbols[i].IsPivotable()) return symbols[i];
		}
		return InvalidSymbol;
	}
};

enum class RelOp : uint8_t {
	LE,
	GE,
	EQ
};

struct Expression {
	float		constant;
	Symbol		terms[MAX_EXPR_TERMS];
	float		coeffs[MAX_EXPR_TERMS];
	uint32_t	term_count;

	Expression() : constant(0), term_count(0) {}

	static Expression Constant(float value) {
		Expression e;
		e.constant = value;
		return e;
	}

	static Expression Term(Symbol sym, float coeff = 1.0f) {
		Expression e;
		e.terms[0] = sym;
		e.coeffs[0] = coeff;
		e.term_count = 1;
		return e;
	}

	void AddTerm(Symbol sym, float coeff) {
		if (term_count < MAX_EXPR_TERMS) {
			for (uint32_t i = 0; i < term_count; ++i) {
				if (terms[i] == sym) {
					coeffs[i] += coeff;
					return;
				}
			}
			terms[term_count] = sym;
			coeffs[term_count] = coeff;
			++term_count;
		}
	}

	void Negate() {
		constant = -constant;
		for (uint32_t i = 0; i < term_count; ++i) {
			coeffs[i] = -coeffs[i];
		}
	}
};

struct Constraint {
	Expression	expr;
	RelOp		op;
	float		strength;

	static Constraint Make(const Expression& e, RelOp op, float strength = Required) {
		Constraint c;
		c.expr = e;
		c.op = op;
		c.strength = strength;
		return c;
	}
};

class Solver {
public:
	Solver() { Reset(); }

	Symbol CreateVariable() {
		if (next_var_id_ >= MAX_VARS) return InvalidSymbol;
		Symbol sym = {next_var_id_++, SymbolType::External};
		return sym;
	}

	bool AddConstraint(const Constraint& constraint) {
		if (constraint_count_ >= MAX_CONSTRAINTS) return false;

		Row row;
		Symbol tag1, tag2;
		CreateRow(constraint, row, tag1, tag2);

		markers_[constraint_count_] = tag1;
		others_[constraint_count_] = tag2;
		++constraint_count_;

		Symbol subject = ChooseSubject(row, tag1, tag2);

		if (subject.IsInvalid()) {
			AddWithArtificialVariable(row);
		} else {
			row.SolveFor(subject);
			SubstituteOut(subject, row);

			if (row_count_ < MAX_ROWS) {
				rows_[row_count_] = row;
				basic_vars_[row_count_] = subject;
				++row_count_;
			}
		}

		Optimize(objective_);

		return infeasible_count_ == 0;
	}

	void RemoveConstraint(uint32_t index) {
		if (index >= constraint_count_) return;

		Symbol marker = markers_[index];
		Symbol other = others_[index];

		if (marker.IsError()) RemoveFromObjective(marker);
		if (other.IsError()) RemoveFromObjective(other);

		Row* row = GetRow(marker);
		if (row) {
			for (uint32_t i = 0; i < row_count_; ++i) {
				if (basic_vars_[i] == marker) {
					rows_[i] = rows_[row_count_ - 1];
					basic_vars_[i] = basic_vars_[row_count_ - 1];
					--row_count_;
					break;
				}
			}
		} else {
			uint32_t best_row = UINT32_MAX;
			float best_ratio = 0;

			for (uint32_t i = 0; i < row_count_; ++i) {
				float coeff = rows_[i].CoefficientFor(marker);
				if (coeff < 0) {
					float ratio = -rows_[i].constant / coeff;
					if (best_row == UINT32_MAX || ratio < best_ratio) {
						best_ratio = ratio;
						best_row = i;
					}
				}
			}

			if (best_row != UINT32_MAX) {
				Pivot(marker, basic_vars_[best_row]);
				for (uint32_t i = 0; i < row_count_; ++i) {
					if (basic_vars_[i] == marker) {
						rows_[i] = rows_[row_count_ - 1];
						basic_vars_[i] = basic_vars_[row_count_ - 1];
						--row_count_;
						break;
					}
				}
			}
		}

		for (uint32_t i = index; i < constraint_count_ - 1; ++i) {
			markers_[i] = markers_[i + 1];
			others_[i] = others_[i + 1];
		}
		--constraint_count_;

		DualOptimize();
	}

	void SuggestValue(Symbol var, float value) {
		(void)var;
		(void)value;
	}

	void UpdateVariables() {
		for (uint32_t i = 0; i < MAX_VARS; ++i) {
			var_values_[i] = 0;
		}

		for (uint32_t i = 0; i < row_count_; ++i) {
			if (basic_vars_[i].IsExternal() && basic_vars_[i].id < MAX_VARS) {
				var_values_[basic_vars_[i].id] = rows_[i].constant;
			}
		}
	}

	float GetValue(Symbol var) const {
		if (!var.IsExternal() || var.id >= MAX_VARS) return 0;
		return var_values_[var.id];
	}

	void Reset() {
		row_count_ = 0;
		constraint_count_ = 0;
		next_var_id_ = 0;
		next_slack_id_ = 0;
		next_error_id_ = 0;
		next_dummy_id_ = 0;
		infeasible_count_ = 0;
		objective_.Clear();
		for (uint32_t i = 0; i < MAX_VARS; ++i) {
			var_values_[i] = 0;
		}
	}

	uint32_t GetConstraintCount() const { return constraint_count_; }
	uint32_t GetInfeasibleCount() const { return infeasible_count_; }
	bool IsFeasible() const { return infeasible_count_ == 0; }

private:
	Symbol CreateSlack() {
		return {next_slack_id_++, SymbolType::Slack};
	}

	Symbol CreateError() {
		return {next_error_id_++, SymbolType::Error};
	}

	Symbol CreateDummy() {
		return {next_dummy_id_++, SymbolType::Dummy};
	}

	void CreateRow(const Constraint& constraint, Row& row, Symbol& tag1,
	               Symbol& tag2) {
		const Expression& expr = constraint.expr;

		row.Clear();
		row.constant = expr.constant;

		for (uint32_t i = 0; i < expr.term_count; ++i) {
			Symbol sym = expr.terms[i];
			float coeff = expr.coeffs[i];

			if (sym.IsExternal()) {
				const Row* symRow = GetRow(sym);
				if (symRow) {
					row.AddRow(*symRow, coeff);
				} else {
					row.AddCoefficient(sym, coeff);
				}
			}
		}

		float strength = constraint.strength;
		if (strength >= Required) strength = Required;

		switch (constraint.op) {
			case RelOp::LE: {
				Symbol slack = CreateSlack();
				tag1 = slack;
				tag2 = InvalidSymbol;
				row.AddCoefficient(slack, 1.0f);
				if (strength < Required) {
					Symbol error = CreateError();
					tag2 = error;
					row.AddCoefficient(error, -1.0f);
					objective_.AddCoefficient(error, strength);
				}
				break;
			}
			case RelOp::GE: {
				Symbol slack = CreateSlack();
				tag1 = slack;
				tag2 = InvalidSymbol;
				row.AddCoefficient(slack, -1.0f);
				if (strength < Required) {
					Symbol error = CreateError();
					tag2 = error;
					row.AddCoefficient(error, 1.0f);
					objective_.AddCoefficient(error, strength);
				}
				break;
			}
			case RelOp::EQ: {
				if (strength < Required) {
					Symbol errplus = CreateError();
					Symbol errminus = CreateError();
					tag1 = errplus;
					tag2 = errminus;
					row.AddCoefficient(errplus, -1.0f);
					row.AddCoefficient(errminus, 1.0f);
					objective_.AddCoefficient(errplus, strength);
					objective_.AddCoefficient(errminus, strength);
				} else {
					Symbol dummy = CreateDummy();
					tag1 = dummy;
					tag2 = InvalidSymbol;
					row.AddCoefficient(dummy, 1.0f);
				}
				break;
			}
		}

		if (row.constant < 0) {
			row.constant = -row.constant;
			for (uint32_t i = 0; i < row.term_count; ++i) {
				row.coeffs[i] = -row.coeffs[i];
			}
		}
	}

	Symbol ChooseSubject(const Row& row, Symbol tag1, Symbol tag2) {
		for (uint32_t i = 0; i < row.term_count; ++i) {
			if (row.symbols[i].IsExternal())
				return row.symbols[i];
		}

		if (tag1.IsPivotable() && row.CoefficientFor(tag1) < 0)
			return tag1;
		if (tag2.IsPivotable() && row.CoefficientFor(tag2) < 0)
			return tag2;

		return InvalidSymbol;
	}

	void AddWithArtificialVariable(const Row& row) {
		Symbol art = {next_slack_id_++, SymbolType::Slack};

		if (row_count_ < MAX_ROWS) {
			rows_[row_count_] = row;
			basic_vars_[row_count_] = art;
			++row_count_;
		}

		Row artificial;
		artificial.AddRow(row, 1.0f);
		Optimize(artificial);

		bool success = fabsf(artificial.constant) < EPSILON;

		Row* art_row_ptr = GetRow(art);
		if (art_row_ptr) {
			Symbol entry = art_row_ptr->AnyPivotableSymbol();
			if (!entry.IsInvalid()) {
				Pivot(entry, art);
			}
		}

		for (uint32_t i = 0; i < row_count_; ++i) {
			rows_[i].SetCoefficient(art, 0);
		}
		objective_.SetCoefficient(art, 0);

		if (!success) {
			++infeasible_count_;
		}
	}

	void Optimize(Row& objective) {
		for (uint32_t iter = 0; iter < 1000; ++iter) {
			Symbol entry = GetEnteringSymbol(objective);
			if (entry.IsInvalid()) break;

			uint32_t leave_row = UINT32_MAX;
			float min_ratio = INFINITY;

			for (uint32_t i = 0; i < row_count_; ++i) {
				float coeff = rows_[i].CoefficientFor(entry);
				if (coeff < -EPSILON) {
					float ratio = -rows_[i].constant / coeff;
					if (ratio < min_ratio) {
						min_ratio = ratio;
						leave_row = i;
					}
				}
			}

			if (leave_row == UINT32_MAX) break;

			Pivot(entry, basic_vars_[leave_row]);
		}
	}

	void DualOptimize() {
		for (uint32_t iter = 0; iter < 1000; ++iter) {
			Symbol leaving = GetLeavingRow();
			if (leaving.IsInvalid()) break;

			Row* row = GetRow(leaving);
			if (!row) break;

			Symbol entering = InvalidSymbol;
			float min_ratio = INFINITY;

			for (uint32_t i = 0; i < row->term_count; ++i) {
				Symbol sym = row->symbols[i];
				float coeff = row->coeffs[i];

				if (coeff > EPSILON && !sym.IsDummy()) {
					float obj_coeff = objective_.CoefficientFor(sym);
					float ratio = obj_coeff / coeff;
					if (ratio < min_ratio) {
						min_ratio = ratio;
						entering = sym;
					}
				}
			}

			if (entering.IsInvalid()) break;

			Pivot(entering, leaving);
		}
	}

	void Pivot(Symbol entry, Symbol exit) {
		Row* exit_row = GetRow(exit);
		if (!exit_row) return;

		exit_row->SolveFor(exit, entry);
		SubstituteOut(entry, *exit_row);

		for (uint32_t i = 0; i < row_count_; ++i) {
			if (basic_vars_[i] == exit) {
				basic_vars_[i] = entry;
				break;
			}
		}
	}

	Row* GetRow(Symbol sym) {
		for (uint32_t i = 0; i < row_count_; ++i) {
			if (basic_vars_[i] == sym)
				return &rows_[i];
		}
		return nullptr;
	}

	const Row* GetRow(Symbol sym) const {
		for (uint32_t i = 0; i < row_count_; ++i) {
			if (basic_vars_[i] == sym)
				return &rows_[i];
		}
		return nullptr;
	}

	Symbol GetEnteringSymbol(const Row& objective) {
		Symbol best = InvalidSymbol;
		float best_coeff = -EPSILON;

		for (uint32_t i = 0; i < objective.term_count; ++i) {
			Symbol sym = objective.symbols[i];
			float coeff = objective.coeffs[i];

			if (!sym.IsDummy() && coeff < best_coeff) {
				best = sym;
				best_coeff = coeff;
			}
		}

		return best;
	}

	Symbol GetLeavingRow() {
		for (uint32_t i = 0; i < row_count_; ++i) {
			if (basic_vars_[i].IsRestricted() && rows_[i].constant < -EPSILON) {
				return basic_vars_[i];
			}
		}
		return InvalidSymbol;
	}

	void RemoveFromObjective(Symbol sym) {
		const Row* row = GetRow(sym);
		if (row) {
			objective_.Substitute(sym, *row);
		} else {
			objective_.SetCoefficient(sym, 0);
		}
	}

	void SubstituteOut(Symbol sym, const Row& row) {
		for (uint32_t i = 0; i < row_count_; ++i) {
			rows_[i].Substitute(sym, row);

			if (basic_vars_[i].IsRestricted() && rows_[i].constant < -EPSILON) {
				if (infeasible_count_ < MAX_ROWS) {
					infeasible_[infeasible_count_++] = basic_vars_[i];
				}
			}
		}
		objective_.Substitute(sym, row);
	}

private:
	Row			rows_[MAX_ROWS];
	Symbol		basic_vars_[MAX_ROWS];
	uint32_t	row_count_;

	Symbol		markers_[MAX_CONSTRAINTS];
	Symbol		others_[MAX_CONSTRAINTS];
	uint32_t	constraint_count_;

	Row			objective_;

	Symbol		infeasible_[MAX_ROWS];
	uint32_t	infeasible_count_;

	float		var_values_[MAX_VARS];

	uint32_t	next_var_id_;
	uint32_t	next_slack_id_;
	uint32_t	next_error_id_;
	uint32_t	next_dummy_id_;
};

} // namespace Cassowary
} // namespace KosmOS

#endif /* _KOSM_CASSOWARY_HPP */
