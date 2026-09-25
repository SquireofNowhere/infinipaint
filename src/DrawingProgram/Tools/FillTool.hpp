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

#pragma once
#include "DrawingProgramToolBase.hpp"
#include <include/core/SkPath.h>

class DrawingProgram;

class FillTool : public DrawingProgramToolBase {
    public:
        FillTool(DrawingProgram& initDrawP);
        virtual DrawingProgramToolType get_type() override;
        virtual void gui_toolbox(Toolbar& t) override;
        virtual void gui_phone_toolbox(PhoneDrawingProgramScreen& t) override;
        virtual void right_click_popup_gui(Toolbar& t, Vector2f popupPos) override;
        virtual void tool_update() override;
        virtual void erase_component(CanvasComponentContainer::ObjInfo* erasedComp) override;
        virtual void draw(SkCanvas* canvas, const DrawData& drawData) override;
        virtual bool prevent_undo_or_redo() override;
        virtual void switch_tool(DrawingProgramToolType newTool) override;
        virtual void input_mouse_button_on_canvas_callback(const InputManager::MouseButtonCallbackArgs& button) override;

        enum class FillResult {
            SUCCESS,
            NOT_ENCLOSED,
            EMPTY
        };

        // Flood fills an RGBA (premultiplied, tightly packed) image starting at seedPos, and outputs the filled area as a path in pixel coordinates
        // tolerance is in the range [0, 1]. gapClosePixels closes gaps up to around (2 * gapClosePixels) wide. expandPixels grows the final fill area
        static FillResult flood_fill_to_path(SkPath& outPath, const uint8_t* rgbaData, int width, int height, const Vector2i& seedPos, float tolerance, int gapClosePixels, int expandPixels);
    private:
        void fill_at(const Vector2f& screenPos);
};
