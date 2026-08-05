#pragma once

void print_impl(const char *caller, const char *fmt, ...);
#define print(fmt, ...) print_impl(__func__, fmt, ##__VA_ARGS__)
