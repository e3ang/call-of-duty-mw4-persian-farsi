/* Logging is optional; failure must never stop subtitle initialization. */
#include <stdarg.h>
static void open_optional_log(HMODULE module) {
    wchar_t path[32768];
    DWORD length=GetModuleFileNameW(module,path,32768);
    if (!length || length>=32768) return;
    wchar_t *slash=wcsrchr(path,L'\\');
    static const wchar_t name[]=L"rtlcompanion.log";
    if (!slash || (size_t)(slash-path)+1+sizeof(name)/sizeof(name[0])>32768) return;
    wcscpy(slash+1,name);
    logfile=_wfopen(path,L"wb");
}
static void log_message(const char *format,...) {
    if (!logfile) return;
    va_list arguments;
    va_start(arguments,format);
    vfprintf(logfile,format,arguments);
    va_end(arguments);
    fflush(logfile);
}
