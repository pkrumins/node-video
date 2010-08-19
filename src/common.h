#ifndef COMMON_H
#define COMMON_H

#include <node.h>
#include <cstring>

v8::Handle<v8::Value> ErrorException(const char *msg);
v8::Handle<v8::Value> VException(const char *msg);

bool str_eq(const char *s1, const char *s2);

typedef enum { BUF_RGB, BUF_BGR, BUF_RGBA, BUF_BGRA } buffer_type;

#endif

