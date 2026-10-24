#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
extern void abort_report_np(const char*,...);
static __attribute__((noreturn)) void report(const char*fmt,...) {char b[2048];va_list a;va_start(a,fmt);int n=vsnprintf(b,sizeof b,fmt,a);va_end(a);write(2,b,n<0?0:n<sizeof b?n:sizeof b-1);_Exit(79);}
__attribute__((used)) static const struct {const void* replace;const void* original;} hooks __attribute__((section("__DATA,__interpose")))={(const void*)report,(const void*)abort_report_np};
