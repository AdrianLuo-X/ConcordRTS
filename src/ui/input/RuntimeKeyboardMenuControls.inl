        addControl(RuntimeMenuAction::KeyboardCategory,RuntimeMenuControlKind::Choice,dialog.imageRect.top+58,
            runtimeMenuLabel(state,"GUI:Category",L"类别"),
            L"‹ "+runtimeKeyboardCategoryLabel(state,state.runtimeMenuKeyboardCategory)+L" ›",0,0,false);
        const auto indexes=runtimeKeyboardCategoryBindings(state);
        const int page=std::clamp(state.runtimeMenuKeyboardPage,0,runtimeKeyboardCategoryPages(state)-1);
        for (int row=0;row<5 && page*5+row<static_cast<int>(indexes.size());++row) {
            const int index=indexes[static_cast<std::size_t>(page*5+row)];
            const auto& [id,keyCode]=state.runtimeMenuKeyboardBindings[static_cast<std::size_t>(index)];
            addControl(runtimeKeyboardActionForIndex(index),RuntimeMenuControlKind::Choice,dialog.imageRect.top+94+row*31,
                runtimeMenuKeyboardBindingLabel(state,id),
                state.runtimeMenuKeyCaptureIndex==index?L"请按新键…":runtimeKeyboardKeyName(keyCode),0,0,false);
        }
        const int top=dialog.imageRect.top+254;
        controls.push_back({RuntimeMenuAction::KeyboardPreviousPage,RuntimeMenuControlKind::Button,
            {left,top,left+86,top+26},L"‹ 上一页",L""});
        controls.push_back({RuntimeMenuAction::KeyboardNextPage,RuntimeMenuControlKind::Button,
            {left+92,top,left+203,top+26},std::to_wstring(page+1)+L" / "+std::to_wstring(runtimeKeyboardCategoryPages(state))+L"  ›",L""});
        controls.push_back({RuntimeMenuAction::KeyboardResetAll,RuntimeMenuControlKind::Button,
            {right-141,top,right,top+26},runtimeMenuLabel(state,"GUI:ResetAll",L"全部重设"),L""});
