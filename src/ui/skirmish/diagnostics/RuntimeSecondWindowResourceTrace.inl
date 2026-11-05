// Private P11 diagnostics only. Optional read-only counts/own-window inventory;
// no altered warmup, input, timing bound, resource assertion or success policy.
void traceSecondWindowResources(const char* label,std::uint32_t baselineGdi,std::uint32_t baselineUser){
 wchar_t enabled[2]{};if(GetEnvironmentVariableW(L"CONCORD_P11_RESOURCE_TRACE",enabled,2)!=1||enabled[0]!=L'1')return;
 std::cout<<"P11 resource observation "<<label<<" baselineGdi="<<baselineGdi<<" baselineUser="<<baselineUser
     <<" currentGdi="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)
     <<" currentUser="<<GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<<"\n";
 CURSORINFO cursor{};cursor.cbSize=sizeof(cursor);const bool gotCursor=GetCursorInfo(&cursor)!=FALSE;
 std::cout<<"P11 cursor observation "<<label<<" threadCursor="<<reinterpret_cast<std::uintptr_t>(GetCursor())
     <<" systemCursor="<<(gotCursor?reinterpret_cast<std::uintptr_t>(cursor.hCursor):0)
     <<" flags="<<(gotCursor?cursor.flags:0)<<" remote="<<GetSystemMetrics(SM_REMOTESESSION)<<"\n";
 MSG pending{};const auto beforePeek=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
 const auto queued=PeekMessageW(&pending,nullptr,0,0,PM_NOREMOVE);
 std::cout<<"P11 queue observation "<<label<<" userBeforePeek="<<beforePeek
     <<" userAfterPeek="<<GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)
     <<" hasQueued="<<(queued!=FALSE)<<" message="<<(queued?pending.message:0)<<"\n";
 EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL{
     DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=GetCurrentProcessId())return TRUE;
     wchar_t name[128]{};GetClassNameW(window,name,128);
     std::cout<<"P11 owned thread window handle="<<reinterpret_cast<std::uintptr_t>(window)
         <<" class="<<std::string(name,name+wcslen(name))<<" visible="<<(IsWindowVisible(window)!=FALSE)<<"\n";
     return TRUE;
 },0);
}
