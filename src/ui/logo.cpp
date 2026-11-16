#include "ui/logo.h"

#include <wrl/client.h>

#include <algorithm>

namespace pitot::ui {
namespace {

using Microsoft::WRL::ComPtr;

// 所有图形都按顺时针走：用 WINDING 填充时，支柱和蒙皮重叠的部分才会合并而不是抠空
void AddArcTo(ID2D1GeometrySink* sink, float x, float y, float radius) {
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y), D2D1::SizeF(radius, radius), 0.0f,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
}

void AddCapsule(ID2D1GeometrySink* sink, float x, float y, float w, float h) {
    const float r = h / 2;
    sink->BeginFigure(D2D1::Point2F(x + r, y), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(x + w - r, y));
    AddArcTo(sink, x + w - r, y + h, r);
    sink->AddLine(D2D1::Point2F(x + r, y + h));
    AddArcTo(sink, x + r, y, r);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
}

}  // namespace

HRESULT CreateLogoGeometry(ID2D1Factory* factory, ID2D1PathGeometry** geometry) {
    if (!factory || !geometry) {
        return E_INVALIDARG;
    }
    ComPtr<ID2D1PathGeometry> path;
    HRESULT hr = factory->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(hr)) {
        hr = path->Open(&sink);
    }
    if (FAILED(hr)) {
        return hr;
    }
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);

    // 管身：圆头迎着来流
    sink->BeginFigure(D2D1::Point2F(108, 92), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(202, 92));
    AddArcTo(sink.Get(), 208, 98, 6);
    sink->AddLine(D2D1::Point2F(208, 118));
    AddArcTo(sink.Get(), 202, 124, 6);
    sink->AddLine(D2D1::Point2F(108, 124));
    AddArcTo(sink.Get(), 108, 92, 16);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    // 后掠支柱，下端伸进蒙皮
    sink->BeginFigure(D2D1::Point2F(124, 134), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(196, 134));
    sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(204, 134), D2D1::Point2F(205, 142)));
    sink->AddLine(D2D1::Point2F(210, 176));
    sink->AddLine(D2D1::Point2F(156, 176));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    AddCapsule(sink.Get(), 116, 172, 100, 16);  // 蒙皮
    AddCapsule(sink.Get(), 48, 68, 60, 12);     // 来流线
    AddCapsule(sink.Get(), 40, 102, 40, 12);
    AddCapsule(sink.Get(), 48, 136, 60, 12);

    hr = sink->Close();
    if (SUCCEEDED(hr)) {
        *geometry = path.Detach();
    }
    return hr;
}

void FillLogo(ID2D1RenderTarget* target, ID2D1Geometry* geometry, ID2D1Brush* brush, const D2D1_RECT_F& box) {
    if (!target || !geometry || !brush) {
        return;
    }
    const float scale = std::min((box.right - box.left) / kLogoWidth, (box.bottom - box.top) / kLogoHeight);
    if (scale <= 0) {
        return;
    }
    const float x = (box.left + box.right - kLogoWidth * scale) / 2 - kLogoLeft * scale;
    const float y = (box.top + box.bottom - kLogoHeight * scale) / 2 - kLogoTop * scale;
    D2D1_MATRIX_3X2_F saved;
    target->GetTransform(&saved);
    target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y) * saved);
    target->FillGeometry(geometry, brush);
    target->SetTransform(saved);
}

}  // namespace pitot::ui
