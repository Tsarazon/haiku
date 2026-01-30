/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <spektr/KosmAutoLayout.hpp>
#include "KosmCassowary.hpp"

namespace KosmOS {
namespace AutoLayout {

using namespace Cassowary;

struct ViewVars {
	Symbol	left;
	Symbol	top;
	Symbol	width;
	Symbol	height;
};

static void BuildAttributeExpression(
	const ViewVars* all_vars,
	uint32_t view_count,
	ViewId view,
	Attribute attr,
	float coeff,
	Expression& expr,
	bool rtl
) {
	if (view < 0 || static_cast<uint32_t>(view) > view_count) return;

	const ViewVars& vars = all_vars[view];

	Attribute effective_attr = attr;
	if (rtl) {
		if (attr == Attribute::Leading) effective_attr = Attribute::Right;
		else if (attr == Attribute::Trailing) effective_attr = Attribute::Left;
	} else {
		if (attr == Attribute::Leading) effective_attr = Attribute::Left;
		else if (attr == Attribute::Trailing) effective_attr = Attribute::Right;
	}

	switch (effective_attr) {
		case Attribute::Left:
			expr.AddTerm(vars.left, coeff);
			break;

		case Attribute::Top:
			expr.AddTerm(vars.top, coeff);
			break;

		case Attribute::Width:
			expr.AddTerm(vars.width, coeff);
			break;

		case Attribute::Height:
			expr.AddTerm(vars.height, coeff);
			break;

		case Attribute::Right:
			expr.AddTerm(vars.left, coeff);
			expr.AddTerm(vars.width, coeff);
			break;

		case Attribute::Bottom:
			expr.AddTerm(vars.top, coeff);
			expr.AddTerm(vars.height, coeff);
			break;

		case Attribute::CenterX:
			expr.AddTerm(vars.left, coeff);
			expr.AddTerm(vars.width, coeff * 0.5f);
			break;

		case Attribute::CenterY:
			expr.AddTerm(vars.top, coeff);
			expr.AddTerm(vars.height, coeff * 0.5f);
			break;

		default:
			break;
	}
}

static Cassowary::Constraint ConvertConstraint(
	const Constraint& c,
	const ViewVars* all_vars,
	uint32_t view_count,
	bool rtl
) {
	Expression expr;
	expr.constant = -c.constant;

	BuildAttributeExpression(all_vars, view_count, c.view1, c.attr1, 1.0f, expr, rtl);

	if (c.view2 >= 0) {
		BuildAttributeExpression(all_vars, view_count, c.view2, c.attr2,
		                         -c.multiplier, expr, rtl);
	}

	RelOp op = RelOp::EQ;
	switch (c.relation) {
		case Relation::Equal:
			op = RelOp::EQ;
			break;
		case Relation::LessOrEqual:
			op = RelOp::LE;
			break;
		case Relation::GreaterOrEqual:
			op = RelOp::GE;
			break;
	}

	return Cassowary::Constraint::Make(expr, op, c.priority);
}

LayoutResult Layout(
	View* views,
	uint32_t view_count,
	const Constraint* constraints,
	uint32_t constraint_count,
	const KosmSize& container_size,
	const AutoLayoutParams& params
) {
	LayoutContext ctx = LayoutContext::Default(container_size);
	ctx.pixel_snap = params.pixel_snap;
	ctx.rtl = params.rtl;
	return Layout(views, view_count, constraints, constraint_count, ctx, params);
}

LayoutResult Layout(
	View* views,
	uint32_t view_count,
	const Constraint* constraints,
	uint32_t constraint_count,
	const LayoutContext& context,
	const AutoLayoutParams& params
) {
	if (view_count == 0)
		return LayoutResult::Success(KosmSize());
	if (view_count > MAX_VIEWS)
		view_count = MAX_VIEWS;

	Solver solver;

	ViewVars all_vars[MAX_VIEWS + 1];

	for (uint32_t i = 0; i <= view_count; ++i) {
		all_vars[i].left = solver.CreateVariable();
		all_vars[i].top = solver.CreateVariable();
		all_vars[i].width = solver.CreateVariable();
		all_vars[i].height = solver.CreateVariable();
	}

	{
		Expression e;
		e.AddTerm(all_vars[0].left, 1.0f);
		solver.AddConstraint(Cassowary::Constraint::Make(e, RelOp::EQ, Required));
	}
	{
		Expression e;
		e.AddTerm(all_vars[0].top, 1.0f);
		solver.AddConstraint(Cassowary::Constraint::Make(e, RelOp::EQ, Required));
	}
	{
		Expression e;
		e.AddTerm(all_vars[0].width, 1.0f);
		e.constant = -context.container_size.width;
		solver.AddConstraint(Cassowary::Constraint::Make(e, RelOp::EQ, Required));
	}
	{
		Expression e;
		e.AddTerm(all_vars[0].height, 1.0f);
		e.constant = -context.container_size.height;
		solver.AddConstraint(Cassowary::Constraint::Make(e, RelOp::EQ, Required));
	}

	for (uint32_t i = 1; i <= view_count; ++i) {
		Expression ew;
		ew.AddTerm(all_vars[i].width, 1.0f);
		solver.AddConstraint(Cassowary::Constraint::Make(ew, RelOp::GE, Required));

		Expression eh;
		eh.AddTerm(all_vars[i].height, 1.0f);
		solver.AddConstraint(Cassowary::Constraint::Make(eh, RelOp::GE, Required));
	}

	for (uint32_t i = 0; i < view_count; ++i) {
		const View& v = views[i];
		if (v.gone) continue;

		const ViewVars& vars = all_vars[i + 1];

		if (v.intrinsic_size.width >= 0) {
			if (v.compression_h > 0) {
				Expression e;
				e.AddTerm(vars.width, 1.0f);
				e.constant = -v.intrinsic_size.width;
				solver.AddConstraint(Cassowary::Constraint::Make(e, RelOp::GE,
				                     v.compression_h));
			}

			if (v.hugging_h > 0) {
				Expression e;
				e.AddTerm(vars.width, 1.0f);
				e.constant = -v.intrinsic_size.width;
				solver.AddConstraint(Cassowary::Constraint::Make(e, RelOp::LE,
				                     v.hugging_h));
			}
		}

		if (v.intrinsic_size.height >= 0) {
			if (v.compression_v > 0) {
				Expression e;
				e.AddTerm(vars.height, 1.0f);
				e.constant = -v.intrinsic_size.height;
				solver.AddConstraint(Cassowary::Constraint::Make(e, RelOp::GE,
				                     v.compression_v));
			}

			if (v.hugging_v > 0) {
				Expression e;
				e.AddTerm(vars.height, 1.0f);
				e.constant = -v.intrinsic_size.height;
				solver.AddConstraint(Cassowary::Constraint::Make(e, RelOp::LE,
				                     v.hugging_v));
			}
		}
	}

	for (uint32_t i = 0; i < constraint_count; ++i) {
		const Constraint& c = constraints[i];
		if (!c.active) continue;

		if (c.view1 > 0 && c.view1 <= static_cast<ViewId>(view_count)
		    && views[c.view1 - 1].gone)
			continue;
		if (c.view2 > 0 && c.view2 <= static_cast<ViewId>(view_count)
		    && views[c.view2 - 1].gone)
			continue;

		Cassowary::Constraint cc = ConvertConstraint(c, all_vars, view_count,
		                                             context.rtl);
		solver.AddConstraint(cc);
	}

	solver.UpdateVariables();

	KosmSize content_size;
	uint32_t unsatisfied = solver.GetInfeasibleCount();

	for (uint32_t i = 0; i < view_count; ++i) {
		if (views[i].gone) {
			views[i].frame = KosmRect();
			continue;
		}

		const ViewVars& vars = all_vars[i + 1];
		float x = solver.GetValue(vars.left);
		float y = solver.GetValue(vars.top);
		float w = solver.GetValue(vars.width);
		float h = solver.GetValue(vars.height);

		if (w < 0) w = 0;
		if (h < 0) h = 0;

		views[i].frame = KosmRect(x, y, w, h);

		if (context.pixel_snap)
			views[i].frame = SnapRectToPixels(views[i].frame);

		float x_end = views[i].frame.x + views[i].frame.width;
		float y_end = views[i].frame.y + views[i].frame.height;
		if (x_end > content_size.width) content_size.width = x_end;
		if (y_end > content_size.height) content_size.height = y_end;
	}

	LayoutResult result;
	result.content_size = content_size;
	result.success = solver.IsFeasible();
	result.line_count = 0;
	result.unsatisfied_count = unsatisfied;

	return result;
}

} // namespace AutoLayout
} // namespace KosmOS
