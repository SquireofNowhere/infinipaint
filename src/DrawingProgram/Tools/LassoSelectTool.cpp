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

#include "LassoSelectTool.hpp"
#include "../DrawingProgram.hpp"
#include "../../MainProgram.hpp"
#include "../../DrawData.hpp"
#include "DrawingProgramToolBase.hpp"
#include "Helpers/MathExtras.hpp"
#include "Helpers/SCollision.hpp"
#include "../../CoordSpaceHelper.hpp"
#include <ranges>
#include <include/core/SkPathBuilder.h>
#include <include/pathops/SkPathOps.h>
#include "../../CanvasComponents/MeshCanvasComponent.hpp"
#include "../../CanvasComponents/CanvasComponentContainer.hpp"
#include "../../Screens/PhoneDrawingProgramScreen.hpp"

#include "../../GUIStuff/ElementHelpers/TextLabelHelpers.hpp"
#include "../../GUIStuff/ElementHelpers/RadioButtonHelpers.hpp"
#include "../../GUIStuff/ElementHelpers/LayoutHelpers.hpp"

LassoSelectTool::LassoSelectTool(DrawingProgram& initDrawP):
    DrawingProgramToolBase(initDrawP)
{}

DrawingProgramToolType LassoSelectTool::get_type() {
    return DrawingProgramToolType::LASSOSELECT;
}

bool LassoSelectTool::is_fill_mode() {
    return drawP.world.main.toolConfig.lassoSelect.fillMode;
}

void LassoSelectTool::fill_mode_gui(const std::function<void(Vector4f*)>& colorButton) {
    using namespace GUIStuff;
    using namespace ElementHelpers;

    auto& gui = drawP.world.main.g.gui;
    radio_button_selector<bool>(gui, "lasso mode", &drawP.world.main.toolConfig.lassoSelect.fillMode, {
        {"Select", false},
        {"Fill", true}
    }, [&] {
        controls = LassoSelectControls();
        drawP.selection.deselect_all();
    });
    if(is_fill_mode()) {
        left_to_right_line_layout(gui, [&]() {
            // Same color as the main color on the left toolbar, so the palette, eyedropper and quick menu all change it too
            colorButton(&drawP.world.main.toolConfig.globalConf.foregroundColor);
            text_label(gui, "Fill Color");
        });
    }
}

void LassoSelectTool::gui_toolbox(Toolbar& t) {
    auto& gui = drawP.world.main.g.gui;
    gui.new_id("lasso select tool", [&] {
        GUIStuff::ElementHelpers::text_label_centered(gui, is_fill_mode() ? "Lasso Fill" : "Lasso Select");
        fill_mode_gui([&](Vector4f* color) { t.color_button_right("Lasso Fill Color", color); });
        if(!is_fill_mode())
            drawP.selection.selection_gui(t);
    });
}

void LassoSelectTool::gui_phone_toolbox(PhoneDrawingProgramScreen& t) {
    auto& gui = drawP.world.main.g.gui;
    gui.new_id("lasso select tool", [&] {
        fill_mode_gui([&](Vector4f* color) { t.color_selector_button("Lasso Fill Color", color); });
        if(!is_fill_mode())
            drawP.selection.phone_selection_gui(t);
    });
}

void LassoSelectTool::right_click_popup_gui(Toolbar& t, Vector2f popupPos) {
    if(is_fill_mode())
        t.paint_popup(popupPos);
    else
        drawP.selection_action_menu(popupPos);
}

void LassoSelectTool::input_key_callback(const InputManager::KeyCallbackArgs& key) {
    if(!is_fill_mode())
        drawP.selection.input_key_callback_modify_selection(key);
}

SkPath LassoSelectTool::get_lasso_path(SkPathFillType fillType) {
    SkPathBuilder lassoPath(fillType);
    lassoPath.moveTo(convert_vec2<SkPoint>(controls.lassoPoints.front()));
    for(Vector2f& p : controls.lassoPoints | std::views::drop(1))
        lassoPath.lineTo(convert_vec2<SkPoint>(p));
    lassoPath.close();
    return lassoPath.detach();
}

void LassoSelectTool::place_lasso_fill() {
    // Winding fill, so a lasso that crosses over itself still fills every loop instead of leaving holes
    std::optional<SkPath> fillPath = Simplify(get_lasso_path(SkPathFillType::kWinding));
    if(!fillPath.has_value() || fillPath->isEmpty())
        return;

    CanvasComponentContainer* newContainer = new CanvasComponentContainer(drawP.world.netObjMan, CanvasComponentType::MESH);
    MeshCanvasComponent& newMesh = static_cast<MeshCanvasComponent&>(newContainer->get_comp());
    newMesh.d.color = drawP.world.main.toolConfig.globalConf.foregroundColor;
    newMesh.d.meshPath = fillPath.value();
    newContainer->coords = controls.coords;
    newContainer->normalize_object_coordinates();

    CanvasComponentContainer::ObjInfo* newObjInfo = drawP.layerMan.add_component_to_layer_being_edited(newContainer);
    drawP.layerMan.add_undo_place_component(newObjInfo);
}

void LassoSelectTool::input_mouse_button_on_canvas_callback(const InputManager::MouseButtonCallbackArgs& button) {
    if(is_fill_mode()) {
        if(button.button == InputManager::MouseButton::LEFT) {
            if(button.down && !controls.isSelecting && drawP.layerMan.is_a_layer_being_edited()) {
                controls = LassoSelectControls();
                controls.coords = drawP.world.drawData.cam.c;
                controls.lassoPoints.emplace_back(button.pos);
                controls.isSelecting = true;
            }
            else if(!button.down && controls.isSelecting) {
                if(controls.lassoPoints.size() > 3 && drawP.layerMan.is_a_layer_being_edited())
                    place_lasso_fill();
                controls = LassoSelectControls();
            }
        }
        return;
    }

    drawP.selection.input_mouse_button_on_canvas_callback_modify_selection(button);
    if(!controls.isSelecting && button.button == InputManager::MouseButton::LEFT && button.down && !drawP.selection.is_being_transformed()) {
        controls = LassoSelectControls();
        controls.coords = drawP.world.drawData.cam.c;
        controls.lassoPoints.emplace_back(button.pos);
        controls.isSelecting = true;
    }
    else if(controls.isSelecting && button.button == InputManager::MouseButton::LEFT && !button.down) {
        if(controls.lassoPoints.size() > 3) {
            SkPathBuilder cCBuild;

            cCBuild.moveTo(convert_vec2<SkPoint>(controls.coords.from_this_to_cam_space(drawP.world, controls.lassoPoints[0])));
            for(size_t i = 1; i < controls.lassoPoints.size(); i++)
                cCBuild.lineTo(convert_vec2<SkPoint>(controls.coords.from_this_to_cam_space(drawP.world, controls.lassoPoints[i])));
            cCBuild.close();

            SkPath cC = cCBuild.detach();

            if(drawP.world.main.input.key(InputManager::KEY_GENERIC_LSHIFT).held)
                drawP.selection.add_from_cam_coord_collider_to_selection(cC, drawP.controls.layerSelector, false);
            else if(drawP.world.main.input.key(InputManager::KEY_GENERIC_LALT).held)
                drawP.selection.remove_from_cam_coord_collider_to_selection(cC, drawP.controls.layerSelector, false);
            else {
                drawP.selection.deselect_all();
                drawP.selection.add_from_cam_coord_collider_to_selection(cC, drawP.controls.layerSelector, false);
            }
            controls.lassoPoints.clear();
        }
        else if(!drawP.world.main.input.key(InputManager::KEY_GENERIC_LSHIFT).held && !drawP.world.main.input.key(InputManager::KEY_GENERIC_LALT).held) {
            drawP.selection.deselect_all();
        }
        controls.isSelecting = false;
        drawP.world.main.g.gui.set_to_layout();
    }
}

void LassoSelectTool::input_mouse_motion_callback(const InputManager::MouseMotionCallbackArgs& motion) {
    if(controls.isSelecting) {
        Vector2f newLassoPoint = controls.coords.from_cam_space_to_this(drawP.world, motion.pos);
        float lassoPointDist = vec_distance(controls.lassoPoints.back(), newLassoPoint);
        if(lassoPointDist > 4.0f)
            controls.lassoPoints.emplace_back(newLassoPoint);
    }
    if(!is_fill_mode())
        drawP.selection.input_mouse_motion_callback_modify_selection(motion);
}

void LassoSelectTool::cancel_finger_touch_callback(const FingerInput::TouchCallbackArgs& touch) {
    // A second finger means the user is moving the camera, so drop the lasso instead of completing it
    if(controls.isSelecting)
        controls = LassoSelectControls();
    else
        DrawingProgramToolBase::cancel_finger_touch_callback(touch);
}

Vector4f* LassoSelectTool::color_picker_color(Vector4f* oldColor) {
    return drawP.selection.color_picker_color(oldColor);
}

void LassoSelectTool::erase_component(CanvasComponentContainer::ObjInfo* erasedComp) {
}

void LassoSelectTool::switch_tool(DrawingProgramToolType newTool) {
    if(!drawP.is_selection_allowing_tool(newTool))
        drawP.selection.deselect_all();
}

void LassoSelectTool::tool_update() {
    // The mode can also be changed from a saved config or the other toolbar, so make sure nothing stays selected while filling
    if(is_fill_mode() && drawP.selection.is_something_selected())
        drawP.selection.deselect_all();
}

bool LassoSelectTool::prevent_undo_or_redo() {
    return (!is_fill_mode() && drawP.selection.is_something_selected()) || controls.isSelecting;
}

void LassoSelectTool::draw(SkCanvas* canvas, const DrawData& drawData) {
    if(controls.isSelecting) {
        canvas->save();
        controls.coords.transform_sk_canvas(canvas, drawData);

        SkPathBuilder lassoPath;
        lassoPath.moveTo(convert_vec2<SkPoint>(controls.lassoPoints.front()));
        for(Vector2f& p : controls.lassoPoints | std::views::drop(1))
            lassoPath.lineTo(convert_vec2<SkPoint>(p));

        SkPath lassoPathDetach = lassoPath.detach();

        if(is_fill_mode()) {
            // Preview what will be filled
            const Vector4f& fillColor = drawP.world.main.toolConfig.globalConf.foregroundColor;
            SkPaint fillPaint;
            fillPaint.setColor4f(SkColor4f{fillColor.x(), fillColor.y(), fillColor.z(), fillColor.w()});
            fillPaint.setAntiAlias(drawData.skiaAA);
            SkPath previewPath = get_lasso_path(SkPathFillType::kWinding);
            canvas->drawPath(previewPath, fillPaint);
        }

        auto paintPair = drawP.select_tool_line_paint(drawData);
        canvas->drawPath(lassoPathDetach, paintPair.first);
        canvas->drawPath(lassoPathDetach, paintPair.second);

        canvas->restore();
    }
}
