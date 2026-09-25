/*
 * InfiniPaint
 * Copyright (C) 2025-2026 Yousef Khadadeh
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "FillTool.hpp"
#include "../DrawingProgram.hpp"
#include "../../MainProgram.hpp"
#include "../../DrawData.hpp"
#include "DrawingProgramToolBase.hpp"
#include "../../CanvasComponents/MeshCanvasComponent.hpp"
#include "../../CanvasComponents/CanvasComponentContainer.hpp"
#include <include/core/SkSurface.h>
#include <include/core/SkPixmap.h>
#include <include/core/SkPathBuilder.h>
#include <clipper2/clipper.h>
#include <Helpers/Logger.hpp>
#include <limits>
#include <cmath>
#include <algorithm>

#include "../../GUIStuff/ElementHelpers/TextLabelHelpers.hpp"
#include "../../GUIStuff/ElementHelpers/NumberSliderHelpers.hpp"

// Maximum distance a traced edge can be moved when smoothing out the pixel staircase of the fill area
#define FILL_TRACE_SIMPLIFY_EPSILON 1.0

FillTool::FillTool(DrawingProgram& initDrawP):
    DrawingProgramToolBase(initDrawP)
{}

DrawingProgramToolType FillTool::get_type() {
    return DrawingProgramToolType::FILL;
}

void FillTool::gui_toolbox(Toolbar& t) {
    using namespace GUIStuff;
    using namespace ElementHelpers;

    auto& gui = drawP.world.main.g.gui;
    auto& fillConf = drawP.world.main.toolConfig.fill;
    gui.new_id("fill tool", [&] {
        text_label_centered(gui, "Fill");
        slider_scalar_field<float>(gui, "tolerance", "Tolerance", &fillConf.tolerance, 0.0f, 100.0f);
        slider_scalar_field<int>(gui, "gap close", "Close Gaps", &fillConf.gapClose, 0, 20);
        slider_scalar_field<int>(gui, "expand", "Expand", &fillConf.expand, 0, 10);
    });
}

void FillTool::gui_phone_toolbox(PhoneDrawingProgramScreen& t) {
    using namespace GUIStuff;
    using namespace ElementHelpers;

    auto& gui = drawP.world.main.g.gui;
    auto& fillConf = drawP.world.main.toolConfig.fill;
    gui.new_id("fill tool", [&] {
        slider_scalar_field<float>(gui, "tolerance", "Tolerance", &fillConf.tolerance, 0.0f, 100.0f);
        slider_scalar_field<int>(gui, "gap close", "Close Gaps", &fillConf.gapClose, 0, 20);
        slider_scalar_field<int>(gui, "expand", "Expand", &fillConf.expand, 0, 10);
    });
}

void FillTool::right_click_popup_gui(Toolbar& t, Vector2f popupPos) {
    t.paint_popup(popupPos);
}

void FillTool::input_mouse_button_on_canvas_callback(const InputManager::MouseButtonCallbackArgs& button) {
    if(button.button == InputManager::MouseButton::LEFT && button.down && drawP.layerMan.is_a_layer_being_edited() && !drawP.selection.is_being_transformed())
        fill_at(button.pos);
}

void FillTool::fill_at(const Vector2f& screenPos) {
    auto& mainProg = drawP.world.main;
    Vector2i imgSize = mainProg.window.size;
    if(imgSize.x() <= 0 || imgSize.y() <= 0)
        return;

    Vector2i seedPos{static_cast<int>(std::floor(screenPos.x())), static_cast<int>(std::floor(screenPos.y()))};
    if(seedPos.x() < 0 || seedPos.y() < 0 || seedPos.x() >= imgSize.x() || seedPos.y() >= imgSize.y())
        return;

    // Render only the canvas contents (no grid, no background, no GUI) so that the fill area is determined by what's drawn on the canvas
    std::vector<uint8_t> imgData((size_t)imgSize.x() * (size_t)imgSize.y() * 4);
    try {
        sk_sp<SkSurface> surface = mainProg.create_native_surface(imgSize, true);
        if(!surface)
            return;
        SkCanvas* canvas = surface->getCanvas();

        DrawData fillDrawData = drawP.world.drawData;
        fillDrawData.takingScreenshot = true;
        fillDrawData.transparentBackground = true;
        fillDrawData.drawGrids = false;
        fillDrawData.skiaAA = mainProg.conf.antialiasing == GlobalConfig::AntiAliasing::SKIA;
        fillDrawData.refresh_draw_optimizing_values();

        canvas->clear(SkColor4f{0.0f, 0.0f, 0.0f, 0.0f});
        drawP.world.draw(canvas, fillDrawData);

        SkImageInfo imgInfo = SkImageInfo::Make(imgSize.x(), imgSize.y(), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        SkPixmap imgPixmap(imgInfo, imgData.data(), (size_t)imgSize.x() * 4);
        if(!surface->readPixels(imgPixmap, 0, 0))
            throw std::runtime_error("Could not read canvas pixels");
    }
    catch(const std::exception& e) {
        Logger::get().log(Logger::LogType::INFO, std::string("[FillTool::fill_at] ") + e.what());
        return;
    }

    auto& fillConf = mainProg.toolConfig.fill;
    SkPath fillPath;
    FillResult result = flood_fill_to_path(fillPath, imgData.data(), imgSize.x(), imgSize.y(), seedPos, std::clamp(fillConf.tolerance, 0.0f, 100.0f) / 100.0f, std::max(fillConf.gapClose, 0), std::max(fillConf.expand, 0));

    switch(result) {
        case FillResult::SUCCESS:
            break;
        case FillResult::NOT_ENCLOSED:
            Logger::get().log(Logger::LogType::USERINFO, "Area to fill isn't enclosed on screen! Close the shape or zoom out");
            return;
        case FillResult::EMPTY:
            return;
    }

    CanvasComponentContainer* newContainer = new CanvasComponentContainer(drawP.world.netObjMan, CanvasComponentType::MESH);
    MeshCanvasComponent& newMesh = static_cast<MeshCanvasComponent&>(newContainer->get_comp());
    newMesh.d.color = mainProg.toolConfig.globalConf.foregroundColor;
    newMesh.d.meshPath = fillPath;
    newContainer->coords = drawP.world.drawData.cam.c;
    newMesh.simplify_paths();
    newContainer->normalize_object_coordinates();

    CanvasComponentContainer::ObjInfo* newObjInfo = drawP.layerMan.add_component_to_layer_being_edited(newContainer);
    drawP.layerMan.add_undo_place_component(newObjInfo);
}

namespace {
    // Grows the set pixels in the mask by radius pixels (approximately circular), using a two pass 3-4 chamfer distance transform
    void dilate_mask(std::vector<uint8_t>& mask, int width, int height, int radius) {
        if(radius <= 0)
            return;

        constexpr uint32_t INF_DIST = std::numeric_limits<uint32_t>::max() / 2;
        std::vector<uint32_t> dist(mask.size());
        for(size_t i = 0; i < mask.size(); i++)
            dist[i] = mask[i] ? 0 : INF_DIST;

        auto relax = [&](uint32_t& d, int x, int y, uint32_t weight) {
            if(x >= 0 && y >= 0 && x < width && y < height)
                d = std::min(d, dist[(size_t)y * width + x] + weight);
        };

        for(int y = 0; y < height; y++) {
            for(int x = 0; x < width; x++) {
                uint32_t& d = dist[(size_t)y * width + x];
                relax(d, x - 1, y, 3);
                relax(d, x - 1, y - 1, 4);
                relax(d, x, y - 1, 3);
                relax(d, x + 1, y - 1, 4);
            }
        }
        for(int y = height - 1; y >= 0; y--) {
            for(int x = width - 1; x >= 0; x--) {
                uint32_t& d = dist[(size_t)y * width + x];
                relax(d, x + 1, y, 3);
                relax(d, x + 1, y + 1, 4);
                relax(d, x, y + 1, 3);
                relax(d, x - 1, y + 1, 4);
            }
        }

        uint32_t maxDist = static_cast<uint32_t>(radius) * 3;
        for(size_t i = 0; i < mask.size(); i++)
            mask[i] = dist[i] <= maxDist ? 1 : 0;
    }

    // 4-connected scanline flood fill over passable pixels
    void flood_fill_mask(std::vector<uint8_t>& region, const std::vector<uint8_t>& passable, int width, int height, const Vector2i& seedPos) {
        auto can_fill = [&](size_t i) {
            return passable[i] && !region[i];
        };

        std::vector<Vector2i> toVisit{seedPos};
        while(!toVisit.empty()) {
            Vector2i p = toVisit.back();
            toVisit.pop_back();

            size_t rowStart = (size_t)p.y() * width;
            if(!can_fill(rowStart + p.x()))
                continue;

            int x0 = p.x();
            while(x0 > 0 && can_fill(rowStart + x0 - 1))
                x0--;
            int x1 = p.x();
            while(x1 < width - 1 && can_fill(rowStart + x1 + 1))
                x1++;
            for(int x = x0; x <= x1; x++)
                region[rowStart + x] = 1;

            for(int y : {p.y() - 1, p.y() + 1}) {
                if(y < 0 || y >= height)
                    continue;
                size_t neighborRowStart = (size_t)y * width;
                bool inSpan = false;
                for(int x = x0; x <= x1; x++) {
                    if(can_fill(neighborRowStart + x)) {
                        if(!inSpan)
                            toVisit.emplace_back(x, y);
                        inSpan = true;
                    }
                    else
                        inSpan = false;
                }
            }
        }
    }

    bool mask_touches_border(const std::vector<uint8_t>& mask, int width, int height) {
        for(int x = 0; x < width; x++) {
            if(mask[x] || mask[(size_t)(height - 1) * width + x])
                return true;
        }
        for(int y = 0; y < height; y++) {
            if(mask[(size_t)y * width] || mask[(size_t)y * width + width - 1])
                return true;
        }
        return false;
    }

    // Converts the set pixels of the mask into polygons, where pixel (x, y) covers the area from (x, y) to (x + 1, y + 1)
    Clipper2Lib::Paths64 trace_mask(const std::vector<uint8_t>& mask, int width, int height) {
        using namespace Clipper2Lib;

        // Horizontal runs of pixels are merged with identical runs in the rows below them, so that fewer rectangles are passed to the union operation
        struct OpenRun {
            int x0;
            int x1;
            int yStart;
        };
        std::vector<OpenRun> openRuns;
        std::vector<OpenRun> nextOpenRuns;
        Paths64 rects;

        auto close_run = [&](const OpenRun& r, int yEnd) {
            rects.emplace_back(Path64{Point64(r.x0, r.yStart), Point64(r.x1, r.yStart), Point64(r.x1, yEnd), Point64(r.x0, yEnd)});
        };

        for(int y = 0; y <= height; y++) {
            nextOpenRuns.clear();
            size_t openIndex = 0;
            if(y < height) {
                size_t rowStart = (size_t)y * width;
                int x = 0;
                while(x < width) {
                    if(!mask[rowStart + x]) {
                        x++;
                        continue;
                    }
                    int x0 = x;
                    while(x < width && mask[rowStart + x])
                        x++;
                    int x1 = x;

                    while(openIndex < openRuns.size() && openRuns[openIndex].x0 < x0)
                        close_run(openRuns[openIndex++], y);
                    if(openIndex < openRuns.size() && openRuns[openIndex].x0 == x0 && openRuns[openIndex].x1 == x1)
                        nextOpenRuns.emplace_back(openRuns[openIndex++]);
                    else
                        nextOpenRuns.emplace_back(OpenRun{x0, x1, y});
                }
            }
            while(openIndex < openRuns.size())
                close_run(openRuns[openIndex++], y);
            std::swap(openRuns, nextOpenRuns);
        }

        if(rects.empty())
            return {};

        return Union(rects, FillRule::NonZero);
    }
}

FillTool::FillResult FillTool::flood_fill_to_path(SkPath& outPath, const uint8_t* rgbaData, int width, int height, const Vector2i& seedPos, float tolerance, int gapClosePixels, int expandPixels) {
    if(width <= 0 || height <= 0 || seedPos.x() < 0 || seedPos.y() < 0 || seedPos.x() >= width || seedPos.y() >= height)
        return FillResult::EMPTY;

    size_t pixelCount = (size_t)width * (size_t)height;
    const uint8_t* seedColor = rgbaData + ((size_t)seedPos.y() * width + seedPos.x()) * 4;
    int maxChannelDiff = static_cast<int>(std::round(tolerance * 255.0f));

    std::vector<uint8_t> fillable(pixelCount);
    for(size_t i = 0; i < pixelCount; i++) {
        const uint8_t* c = rgbaData + i * 4;
        int diff = 0;
        for(int j = 0; j < 4; j++)
            diff = std::max(diff, std::abs(static_cast<int>(c[j]) - static_cast<int>(seedColor[j])));
        fillable[i] = diff <= maxChannelDiff ? 1 : 0;
    }

    std::vector<uint8_t> passable;
    if(gapClosePixels > 0) {
        // Thicken everything that blocks the fill so that small gaps in outlines are closed
        std::vector<uint8_t> blocked(pixelCount);
        for(size_t i = 0; i < pixelCount; i++)
            blocked[i] = !fillable[i];
        dilate_mask(blocked, width, height, gapClosePixels);
        passable.resize(pixelCount);
        for(size_t i = 0; i < pixelCount; i++)
            passable[i] = !blocked[i];
        // Clicked in an area too thin to survive closing the gaps, fill without closing gaps instead
        if(!passable[(size_t)seedPos.y() * width + seedPos.x()]) {
            passable = fillable;
            gapClosePixels = 0;
        }
    }
    else
        passable = fillable;

    std::vector<uint8_t> region(pixelCount, 0);
    flood_fill_mask(region, passable, width, height, seedPos);

    // The canvas is infinite, so an area that reaches the edge of the screen can't be known to be enclosed
    if(mask_touches_border(region, width, height))
        return FillResult::NOT_ENCLOSED;

    if(gapClosePixels > 0) {
        // Grow back into the space that was taken up by thickening the outlines
        dilate_mask(region, width, height, gapClosePixels);
        for(size_t i = 0; i < pixelCount; i++)
            region[i] = region[i] && fillable[i];
    }

    dilate_mask(region, width, height, expandPixels);

    Clipper2Lib::Paths64 polygons = Clipper2Lib::SimplifyPaths(trace_mask(region, width, height), FILL_TRACE_SIMPLIFY_EPSILON);

    SkPathBuilder pathBuilder(SkPathFillType::kWinding);
    bool pathEmpty = true;
    for(const Clipper2Lib::Path64& polygon : polygons) {
        if(polygon.size() < 3)
            continue;
        pathBuilder.moveTo(static_cast<float>(polygon[0].x), static_cast<float>(polygon[0].y));
        for(size_t i = 1; i < polygon.size(); i++)
            pathBuilder.lineTo(static_cast<float>(polygon[i].x), static_cast<float>(polygon[i].y));
        pathBuilder.close();
        pathEmpty = false;
    }

    if(pathEmpty)
        return FillResult::EMPTY;

    outPath = pathBuilder.detach();
    return FillResult::SUCCESS;
}

void FillTool::erase_component(CanvasComponentContainer::ObjInfo* erasedComp) {
}

void FillTool::tool_update() {
}

bool FillTool::prevent_undo_or_redo() {
    return false;
}

void FillTool::draw(SkCanvas* canvas, const DrawData& drawData) {
}

void FillTool::switch_tool(DrawingProgramToolType newTool) {
}
