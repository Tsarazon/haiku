/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender_PathGeometry.cpp - Path processing and geometry operations using AGG
 */

#include "AGGRender.h"

#include <agg_basics.h>
#include <agg_path_storage.h>
#include <agg_conv_curve.h>
#include <agg_conv_transform.h>
#include <agg_conv_dash.h>
#include <agg_conv_contour.h>
#include <agg_conv_smooth_poly1.h>
#include <agg_conv_clip_polygon.h>
#include <agg_conv_clip_polyline.h>
#include <agg_ellipse.h>
#include <agg_rounded_rect.h>
#include <agg_arc.h>
#include <agg_bezier_arc.h>
#include <agg_trans_affine.h>

#include <SupportDefs.h>
#include <math.h>


// Path command creation and validation
uint32
AGGRender::CreatePathCommand(path_cmd_e cmd, float x, float y)
{
	switch (cmd) {
		case PATH_CMD_MOVE_TO:
			return agg::path_cmd_move_to;
		case PATH_CMD_LINE_TO:
			return agg::path_cmd_line_to;
		case PATH_CMD_CURVE4:
			return agg::path_cmd_curve4;
		case PATH_CMD_CLOSE:
			return agg::path_cmd_end_poly;
		case PATH_CMD_STOP:
			return agg::path_cmd_stop;
		default:
			return agg::path_cmd_stop;
	}
}


bool
AGGRender::IsPathCommandType(uint32 cmd, path_cmd_e type)
{
	switch (type) {
		case PATH_CMD_MOVE_TO:
			return agg::is_move_to(cmd);
		case PATH_CMD_LINE_TO:
			return agg::is_line_to(cmd);
		case PATH_CMD_CURVE4:
			return agg::is_curve(cmd);
		case PATH_CMD_CLOSE:
			return agg::is_close(cmd);
		case PATH_CMD_STOP:
			return agg::is_stop(cmd);
		case PATH_CMD_VERTEX:
			return agg::is_vertex(cmd);
		default:
			return false;
	}
}


// Arc creation methods
status_t
AGGRender::CreateArc(RenderPath* path, float cx, float cy, float rx, float ry, 
					 float startAngle, float endAngle)
{
	if (path == NULL)
		return B_BAD_VALUE;

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	agg::arc arc(cx, cy, rx, ry, startAngle, endAngle);
	storage->join_path(arc);

	return B_OK;
}


status_t
AGGRender::CreateBezierArc(RenderPath* path, float cx, float cy, float rx, float ry,
						   float startAngle, float sweepAngle)
{
	if (path == NULL)
		return B_BAD_VALUE;

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	agg::bezier_arc bezArc(cx, cy, rx, ry, startAngle, sweepAngle);
	storage->join_path(bezArc);

	return B_OK;
}


status_t
AGGRender::CreateSVGArc(RenderPath* path, float rx, float ry, float angle,
						bool largeArcFlag, bool sweepFlag, float x, float y)
{
	if (path == NULL)
		return B_BAD_VALUE;

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	// Get current position for SVG arc calculation
	double lastX, lastY;
	uint32 lastCmd = storage->last_vertex(&lastX, &lastY);
	if (!agg::is_vertex(lastCmd)) {
		lastX = 0;
		lastY = 0;
	}

	agg::bezier_arc_svg svgArc(lastX, lastY, rx, ry, angle, largeArcFlag, sweepFlag, x, y);
	storage->join_path(svgArc);

	return B_OK;
}


// Ellipse creation
status_t
AGGRender::CreateEllipse(RenderPath* path, float cx, float cy, float rx, float ry)
{
	if (path == NULL)
		return B_BAD_VALUE;

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	agg::ellipse ellipse(cx, cy, rx, ry);
	storage->join_path(ellipse);

	return B_OK;
}


// Rounded rectangle creation
status_t
AGGRender::CreateRoundedRect(RenderPath* path, float x1, float y1, float x2, float y2,
							 float r)
{
	if (path == NULL)
		return B_BAD_VALUE;

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	agg::rounded_rect roundedRect(x1, y1, x2, y2, r);
	storage->join_path(roundedRect);

	return B_OK;
}


status_t
AGGRender::CreateRoundedRectVarying(RenderPath* path, float x1, float y1, float x2, float y2,
									float rx1, float ry1, float rx2, float ry2,
									float rx3, float ry3, float rx4, float ry4)
{
	if (path == NULL)
		return B_BAD_VALUE;

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	agg::rounded_rect roundedRect;
	roundedRect.rect(x1, y1, x2, y2);
	roundedRect.radius(rx1, ry1, rx2, ry2, rx3, ry3, rx4, ry4);
	storage->join_path(roundedRect);

	return B_OK;
}


// Path converters
status_t
AGGRender::ConvertPathToCurves(RenderPath* source, RenderPath** result)
{
	if (source == NULL || result == NULL)
		return B_BAD_VALUE;

	AGGPath* sourcePath = static_cast<AGGPath*>(source);
	agg::path_storage* sourceStorage = static_cast<agg::path_storage*>(sourcePath->fPath);

	*result = CreatePath();
	AGGPath* resultPath = static_cast<AGGPath*>(*result);
	agg::path_storage* resultStorage = static_cast<agg::path_storage*>(resultPath->fPath);

	typedef agg::conv_curve<agg::path_storage> curve_type;
	curve_type curves(*sourceStorage);
	
	resultStorage->join_path(curves);

	return B_OK;
}


status_t
AGGRender::TransformPath(RenderPath* path, const BAffineTransform& transform)
{
	if (path == NULL)
		return B_BAD_VALUE;

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	// Convert BAffineTransform to agg::trans_affine
	agg::trans_affine aggTransform(
		transform.sx, transform.shy, transform.shx,
		transform.sy, transform.tx, transform.ty);

	typedef agg::conv_transform<agg::path_storage> transform_type;
	transform_type transformed(*storage, aggTransform);
	
	// Replace path with transformed version
	storage->remove_all();
	storage->join_path(transformed);

	return B_OK;
}


status_t
AGGRender::AddDashToPath(RenderPath* source, RenderPath** result,
						 const float* dashArray, int32 dashCount, float dashStart)
{
	if (source == NULL || result == NULL || dashArray == NULL || dashCount <= 0)
		return B_BAD_VALUE;

	AGGPath* sourcePath = static_cast<AGGPath*>(source);
	agg::path_storage* sourceStorage = static_cast<agg::path_storage*>(sourcePath->fPath);

	*result = CreatePath();
	AGGPath* resultPath = static_cast<AGGPath*>(*result);
	agg::path_storage* resultStorage = static_cast<agg::path_storage*>(resultPath->fPath);

	typedef agg::conv_dash<agg::path_storage> dash_type;
	dash_type dashed(*sourceStorage);

	// Add dash pattern
	for (int32 i = 0; i < dashCount; i += 2) {
		float dashLength = dashArray[i];
		float gapLength = (i + 1 < dashCount) ? dashArray[i + 1] : dashLength;
		dashed.add_dash(dashLength, gapLength);
	}

	if (dashStart != 0.0f)
		dashed.dash_start(dashStart);

	resultStorage->join_path(dashed);

	return B_OK;
}


status_t
AGGRender::SmoothPath(RenderPath* source, RenderPath** result, float smoothValue)
{
	if (source == NULL || result == NULL)
		return B_BAD_VALUE;

	AGGPath* sourcePath = static_cast<AGGPath*>(source);
	agg::path_storage* sourceStorage = static_cast<agg::path_storage*>(sourcePath->fPath);

	*result = CreatePath();
	AGGPath* resultPath = static_cast<AGGPath*>(*result);
	agg::path_storage* resultStorage = static_cast<agg::path_storage*>(resultPath->fPath);

	typedef agg::conv_smooth_poly1<agg::path_storage> smooth_type;
	smooth_type smoothed(*sourceStorage);
	smoothed.smooth_value(smoothValue);

	resultStorage->join_path(smoothed);

	return B_OK;
}


status_t
AGGRender::CreateContourPath(RenderPath* source, RenderPath** result,
							 float width, bool counterClockwise)
{
	if (source == NULL || result == NULL)
		return B_BAD_VALUE;

	AGGPath* sourcePath = static_cast<AGGPath*>(source);
	agg::path_storage* sourceStorage = static_cast<agg::path_storage*>(sourcePath->fPath);

	*result = CreatePath();
	AGGPath* resultPath = static_cast<AGGPath*>(*result);
	agg::path_storage* resultStorage = static_cast<agg::path_storage*>(resultPath->fPath);

	typedef agg::conv_contour<agg::path_storage> contour_type;
	contour_type contoured(*sourceStorage);
	contoured.width(width);
	
	if (counterClockwise)
		contoured.auto_detect_orientation(false);

	resultStorage->join_path(contoured);

	return B_OK;
}


// Path clipping operations
status_t
AGGRender::ClipPathToPolygon(RenderPath* source, RenderPath** result,
							 const BPoint* vertices, int32 count)
{
	if (source == NULL || result == NULL || vertices == NULL || count < 3)
		return B_BAD_VALUE;

	AGGPath* sourcePath = static_cast<AGGPath*>(source);
	agg::path_storage* sourceStorage = static_cast<agg::path_storage*>(sourcePath->fPath);

	// Create clipping polygon
	agg::path_storage clipPolygon;
	clipPolygon.move_to(vertices[0].x, vertices[0].y);
	for (int32 i = 1; i < count; i++) {
		clipPolygon.line_to(vertices[i].x, vertices[i].y);
	}
	clipPolygon.close_polygon();

	*result = CreatePath();
	AGGPath* resultPath = static_cast<AGGPath*>(*result);
	agg::path_storage* resultStorage = static_cast<agg::path_storage*>(resultPath->fPath);

	// Calculate bounding box of clip polygon
	double minX = vertices[0].x, minY = vertices[0].y;
	double maxX = vertices[0].x, maxY = vertices[0].y;
	for (int32 i = 1; i < count; i++) {
		if (vertices[i].x < minX) minX = vertices[i].x;
		if (vertices[i].x > maxX) maxX = vertices[i].x;
		if (vertices[i].y < minY) minY = vertices[i].y;
		if (vertices[i].y > maxY) maxY = vertices[i].y;
	}

	typedef agg::conv_clip_polygon<agg::path_storage> clip_type;
	clip_type clipped(*sourceStorage);
	clipped.clip_box(minX, minY, maxX, maxY);

	resultStorage->join_path(clipped);

	return B_OK;
}


status_t
AGGRender::ClipPathToPolyline(RenderPath* source, RenderPath** result,
							  const BPoint* vertices, int32 count)
{
	if (source == NULL || result == NULL || vertices == NULL || count < 2)
		return B_BAD_VALUE;

	AGGPath* sourcePath = static_cast<AGGPath*>(source);
	agg::path_storage* sourceStorage = static_cast<agg::path_storage*>(sourcePath->fPath);

	// Create clipping polyline
	agg::path_storage clipPolyline;
	clipPolyline.move_to(vertices[0].x, vertices[0].y);
	for (int32 i = 1; i < count; i++) {
		clipPolyline.line_to(vertices[i].x, vertices[i].y);
	}

	*result = CreatePath();
	AGGPath* resultPath = static_cast<AGGPath*>(*result);
	agg::path_storage* resultStorage = static_cast<agg::path_storage*>(resultPath->fPath);

	// Calculate bounding box of clip polyline
	double minX = vertices[0].x, minY = vertices[0].y;
	double maxX = vertices[0].x, maxY = vertices[0].y;
	for (int32 i = 1; i < count; i++) {
		if (vertices[i].x < minX) minX = vertices[i].x;
		if (vertices[i].x > maxX) maxX = vertices[i].x;
		if (vertices[i].y < minY) minY = vertices[i].y;
		if (vertices[i].y > maxY) maxY = vertices[i].y;
	}

	typedef agg::conv_clip_polyline<agg::path_storage> clip_type;
	clip_type clipped(*sourceStorage);
	clipped.clip_box(minX, minY, maxX, maxY);

	resultStorage->join_path(clipped);

	return B_OK;
}


// Path command validation utilities
bool
AGGRender::ValidatePathCommand(uint32 cmd)
{
	return agg::is_vertex(cmd) || agg::is_stop(cmd);
}


bool
AGGRender::IsPathCommandMoveTo(uint32 cmd)
{
	return agg::is_move_to(cmd);
}


bool
AGGRender::IsPathCommandLineTo(uint32 cmd)
{
	return agg::is_line_to(cmd);
}


bool
AGGRender::IsPathCommandCurve(uint32 cmd)
{
	return agg::is_curve(cmd);
}


bool
AGGRender::IsPathCommandClose(uint32 cmd)
{
	return agg::is_close(cmd);
}


bool
AGGRender::IsPathCommandStop(uint32 cmd)
{
	return agg::is_stop(cmd);
}


bool
AGGRender::IsPathCommandVertex(uint32 cmd)
{
	return agg::is_vertex(cmd);
}


// Path geometry utilities
float
AGGRender::CalculatePathLength(RenderPath* path)
{
	if (path == NULL)
		return 0.0f;

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	float totalLength = 0.0f;
	double lastX = 0.0, lastY = 0.0;
	bool hasStart = false;

	storage->rewind(0);
	uint32 cmd;
	double x, y;

	while (!agg::is_stop(cmd = storage->vertex(&x, &y))) {
		if (agg::is_vertex(cmd)) {
			if (hasStart && !agg::is_move_to(cmd)) {
				double dx = x - lastX;
				double dy = y - lastY;
				totalLength += sqrt(dx * dx + dy * dy);
			}
			lastX = x;
			lastY = y;
			hasStart = true;
		}
	}

	return totalLength;
}


BRect
AGGRender::CalculatePathBoundingRect(RenderPath* path)
{
	if (path == NULL)
		return BRect();

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	double minX = FLT_MAX, minY = FLT_MAX;
	double maxX = -FLT_MAX, maxY = -FLT_MAX;
	bool hasPoints = false;

	storage->rewind(0);
	uint32 cmd;
	double x, y;

	while (!agg::is_stop(cmd = storage->vertex(&x, &y))) {
		if (agg::is_vertex(cmd)) {
			minX = min_c(minX, x);
			minY = min_c(minY, y);
			maxX = max_c(maxX, x);
			maxY = max_c(maxY, y);
			hasPoints = true;
		}
	}

	if (!hasPoints)
		return BRect();

	return BRect(minX, minY, maxX, maxY);
}