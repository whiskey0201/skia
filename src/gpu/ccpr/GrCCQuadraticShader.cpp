/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrCCQuadraticShader.h"

#include "glsl/GrGLSLFragmentShaderBuilder.h"
#include "glsl/GrGLSLVertexGeoBuilder.h"

void GrCCQuadraticShader::emitSetupCode(GrGLSLVertexGeoBuilder* s, const char* pts,
                                        const char* wind, const char** outHull4) const {
    s->declareGlobal(fQCoordMatrix);
    s->codeAppendf("%s = float2x2(1, 1, .5, 0) * inverse(float2x2(%s[2] - %s[0], %s[1] - %s[0]));",
                   fQCoordMatrix.c_str(), pts, pts, pts, pts);

    s->declareGlobal(fQCoord0);
    s->codeAppendf("%s = %s[0];", fQCoord0.c_str(), pts);

    s->declareGlobal(fEdgeDistanceEquation);
    s->codeAppendf("float2 edgept0 = %s[%s > 0 ? 2 : 0];", pts, wind);
    s->codeAppendf("float2 edgept1 = %s[%s > 0 ? 0 : 2];", pts, wind);
    Shader::EmitEdgeDistanceEquation(s, "edgept0", "edgept1", fEdgeDistanceEquation.c_str());

    if (outHull4) {
        // Clip the bezier triangle by the tangent line at maximum height. Quadratics have the nice
        // property that maximum height always occurs at T=.5. This is a simple application for
        // De Casteljau's algorithm.
        s->codeAppendf("float2 quadratic_hull[4] = float2[4](%s[0], "
                                                            "(%s[0] + %s[1]) * .5, "
                                                            "(%s[1] + %s[2]) * .5, "
                                                            "%s[2]);",
                                                            pts, pts, pts, pts, pts, pts);
        *outHull4 = "quadratic_hull";
    }
}

void GrCCQuadraticShader::onEmitVaryings(GrGLSLVaryingHandler* varyingHandler,
                                         GrGLSLVarying::Scope scope, SkString* code,
                                         const char* position, const char* coverage,
                                         const char* cornerCoverage) {
    fCoord_fGrad.reset(kFloat4_GrSLType, scope);
    varyingHandler->addVarying("coord_and_grad", &fCoord_fGrad);
    code->appendf("%s.xy = %s * (%s - %s);", // Quadratic coords.
                  OutName(fCoord_fGrad), fQCoordMatrix.c_str(), position, fQCoord0.c_str());
    code->appendf("%s.zw = 2*bloat * float2(2 * %s.x, -1) * %s;", // Gradient.
                  OutName(fCoord_fGrad), OutName(fCoord_fGrad), fQCoordMatrix.c_str());

    // Coverages need full precision since distance to the opposite edge can be large.
    fEdge_fWind_fCorner.reset(cornerCoverage ? kFloat4_GrSLType : kFloat2_GrSLType, scope);
    varyingHandler->addVarying("edge_and_wind_and_corner", &fEdge_fWind_fCorner);
    code->appendf("float edge = dot(%s, float3(%s, 1));", // Distance to flat opposite edge.
                  fEdgeDistanceEquation.c_str(), position);
    code->appendf("%s.x = edge;", OutName(fEdge_fWind_fCorner));
    code->appendf("%s.y = %s;", OutName(fEdge_fWind_fCorner), coverage); // coverage == wind.

    if (cornerCoverage) {
        code->appendf("half hull_coverage;");
        this->calcHullCoverage(code, OutName(fCoord_fGrad), "edge", "hull_coverage");
        code->appendf("%s.zw = half2(hull_coverage, 1) * %s;",
                      OutName(fEdge_fWind_fCorner), cornerCoverage);
    }
}

void GrCCQuadraticShader::onEmitFragmentCode(GrGLSLFPFragmentBuilder* f,
                                             const char* outputCoverage) const {
    this->calcHullCoverage(&AccessCodeString(f), fCoord_fGrad.fsIn(),
                           SkStringPrintf("%s.x", fEdge_fWind_fCorner.fsIn()).c_str(),
                           outputCoverage);
    f->codeAppendf("%s *= %s.y;", outputCoverage, fEdge_fWind_fCorner.fsIn()); // Wind.

    if (kFloat4_GrSLType == fEdge_fWind_fCorner.type()) {
        f->codeAppendf("%s = %s.z * %s.w + %s;",// Attenuated corner coverage.
                       outputCoverage, fEdge_fWind_fCorner.fsIn(), fEdge_fWind_fCorner.fsIn(),
                       outputCoverage);
    }
}

void GrCCQuadraticShader::calcHullCoverage(SkString* code, const char* coordAndGrad,
                                           const char* edge, const char* outputCoverage) const {
    code->appendf("float x = %s.x, y = %s.y;", coordAndGrad, coordAndGrad);
    code->appendf("float2 grad = %s.zw;", coordAndGrad);
    code->append ("float f = x*x - y;");
    code->append ("float fwidth = abs(grad.x) + abs(grad.y);");
    code->appendf("%s = min(0.5 - f/fwidth, 1);", outputCoverage); // Curve coverage.
    code->appendf("half d = min(%s, 0);", edge); // Flat edge opposite the curve.
    code->appendf("%s = max(%s + d, 0);", outputCoverage, outputCoverage); // Total hull coverage.
}
